#include "SheetNestingPlaneStrategies.h"
#include "SheetNestingConstructionTrace.h"
#include "NestingAxisSafety.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <iomanip>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <tuple>
#include <vector>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool condition,const std::string& reason)
{if(!condition)throw std::runtime_error(reason);}

Shape Rectangle(double width,double height)
{Shape shape;shape.Outer={{0,0},{width,0},{width,height},{0,height}};return shape;}

Part Original(std::string id,Shape shape,std::size_t quantity=1,std::vector<double> angles={0})
{
    Part part;part.ID=std::move(id);part.Geometry=std::move(shape);part.Quantity=quantity;
    part.Material="metal";part.FreeRotation=false;part.AllowedAnglesDegrees=std::move(angles);return part;
}

Options Budget()
{
    Options options;options.TimeLimitSeconds=30;options.MaxPlacementAttempts=2000000;
    options.EnableBasicCompaction=false;options.EnableCombinations=false;options.Ranking=RankingPolicy::LowThenLeft;
    return options;
}

PlaneStrategySettings Constructor(PlaneStrategyKind kind)
{
    PlaneStrategySettings settings;settings.Kind=kind;settings.NfpTrials=1;
    settings.EnableLayoutRefinement=false;settings.EnableContourSettling=false;settings.EnableAllowedAngleSettling=false;
    settings.EnableSheetTemplateReuse=false;settings.MaxNodes=128;settings.BranchWidth=4;settings.BranchDecisions=3;
    return settings;
}

Problem Board(std::vector<Part> parts,double width,double height,bool common=true)
{
    Problem problem;problem.Parts=std::move(parts);problem.Stocks={{"sheet",width,height,1,1,"metal"}};
    problem.EdgeMargin=.5;problem.PartGap=.2;problem.Tolerance=1e-7;problem.AllowCommonEdges=common;return problem;
}

bool SameLayouts(const std::vector<Layout>& first,const std::vector<Layout>& second)
{
    if(first.size()!=second.size())return false;
    for(std::size_t i=0;i<first.size();++i)
    {
        const auto& a=first[i];const auto& b=second[i];
        if(a.StockID!=b.StockID||a.StockInstance!=b.StockInstance||a.Width!=b.Width||a.Height!=b.Height||a.Placements.size()!=b.Placements.size())return false;
        for(std::size_t j=0;j<a.Placements.size();++j)
        {
            const auto& x=a.Placements[j];const auto& y=b.Placements[j];
            if(x.PartID!=y.PartID||x.Instance!=y.Instance||x.Transform.X!=y.Transform.X||x.Transform.Y!=y.Transform.Y||
                x.Transform.AngleDegrees!=y.Transform.AngleDegrees||x.Group!=y.Group||x.GapFiller!=y.GapFiller)return false;
        }
    }
    return true;
}

Point World(Point point,Pose pose)
{
    constexpr double radians=3.14159265358979323846/180;
    const double c=std::cos(pose.AngleDegrees*radians),s=std::sin(pose.AngleDegrees*radians);
    return {pose.X+point.X*c-point.Y*s,pose.Y+point.X*s+point.Y*c};
}

double SharedLength(const std::vector<Point>& first,Pose a,const std::vector<Point>& second,Pose b)
{
    double total=0;
    for(std::size_t i=0;i<first.size();++i)
    {
        const auto p=World(first[i],a),q=World(first[(i+1)%first.size()],a);
        const double dx=q.X-p.X,dy=q.Y-p.Y,length=std::hypot(dx,dy);
        if(length<=1e-10)continue;
        for(std::size_t j=0;j<second.size();++j)
        {
            const auto r=World(second[j],b),s=World(second[(j+1)%second.size()],b);
            const auto distance=[&](Point v){return std::abs((v.X-p.X)*dy-(v.Y-p.Y)*dx)/length;};
            if(distance(r)>1e-7||distance(s)>1e-7)continue;
            const auto project=[&](Point v){return ((v.X-p.X)*dx+(v.Y-p.Y)*dy)/length;};
            const double t0=project(r),t1=project(s);
            total+=std::max(0.,std::min(length,std::max(t0,t1))-std::max(0.,std::min(t0,t1)));
        }
    }
    return total;
}

void RectanglesAndTrace(PlaneStrategyKind kind,const char* name,double angle)
{
    // With a positive ordinary gap, this exact usable rectangle cannot hold
    // both originals unless construction actually proposes a shared segment.
    const auto problem=Board({Original("same-original",Rectangle(3,2),2,{angle})},angle==0?7:3,angle==0?3:7);
    const auto settings=Constructor(kind);auto options=Budget();
    const auto baseline=MakePlaneStrategyFactory(settings)()->Run(problem,options);
    auto checked=Validate(problem,baseline);
    Require(checked.Valid&&checked.Complete&&baseline.Layouts.size()==1,std::string(name)+" missed exact-width common-edge construction");
    const auto& placed=baseline.Layouts[0].Placements;
    Require(placed.size()==2&&placed[0].Transform.AngleDegrees==angle&&placed[1].Transform.AngleDegrees==angle,
        std::string(name)+" changed the only allowed original angle or instance count");
    Require(SharedLength(problem.Parts[0].Geometry.Outer,placed[0].Transform,problem.Parts[0].Geometry.Outer,placed[1].Transform)>1.99,
        std::string(name)+" did not construct a positive-length shared original edge");

    auto separated=problem;separated.AllowCommonEdges=false;
    const auto ordinary=MakePlaneStrategyFactory(settings)()->Run(separated,options);
    const auto ordinaryCheck=Validate(separated,ordinary);
    Require(ordinaryCheck.Valid&&ordinaryCheck.PlacedCount==1&&!ordinaryCheck.Complete,
        std::string(name)+" silently waived the ordinary .2 gap when common edges were disabled");

    std::vector<ConstructionTraceEvent> events;
    options.ConstructionTraceRoute=name;options.ConstructionTraceScope="common-edge-contract";
    options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
    options.ConstructionTrace=[&](const auto& event){events.push_back(event);};
    const auto traced=MakePlaneStrategyFactory(settings)()->Run(problem,options);
    Require(SameLayouts(baseline.Layouts,traced.Layouts)&&baseline.Stats.PlacementAttempts==traced.Stats.PlacementAttempts,
        std::string(name)+" common-edge observation changed deterministic results or attempt accounting");
    Require(!options.ConstructionObservation->Failed.load()&&!options.ConstructionObservation->Omitted.load(),
        std::string(name)+" common-edge observation silently lost events");
    std::set<std::tuple<std::string,std::uint64_t,std::uint64_t>> candidates;
    std::map<std::pair<std::string,std::uint64_t>,Layout> states;
    bool sawCommit=false,sawFinal=false;
    for(const auto& event:events)
    {
        if(event.CandidateID&&(event.Stage==ConstructionTraceStage::Generate||event.Stage==ConstructionTraceStage::Filter||event.Stage==ConstructionTraceStage::Score))
            candidates.emplace(event.Scope,event.ProposalID,event.CandidateID);
        if(event.Stage==ConstructionTraceStage::Commit)
        {
            sawCommit=true;
            Require(event.CandidateID&&candidates.contains({event.Scope,event.ProposalID,event.CandidateID}),
                std::string(name)+" common-edge commit cannot be linked to an observed native candidate");
            Require(event.StateID&&event.Layouts.size()==1&&!event.Members.empty(),std::string(name)+" common-edge commit lacks actual state/delta");
            if(event.ParentStateID)
            {
                const auto parent=states.find({event.Scope,event.ParentStateID});
                Require(parent!=states.end()&&event.Layouts[0].Placements.size()==parent->second.Placements.size()+event.Members.size(),
                    std::string(name)+" common-edge state is not its recorded parent plus originals");
            }
            states[{event.Scope,event.StateID}]=event.Layouts[0];
        }
        if(event.Source=="strategy.result")
        {sawFinal=true;Require(SameLayouts(event.Layouts,traced.Layouts),std::string(name)+" traced final state differs from its actual return");}
    }
    Require(sawCommit&&sawFinal,std::string(name)+" common-edge construction omitted commit/final evidence");
}

void SlopedSharedEdge()
{
    Shape triangle;triangle.Outer={{0,0},{10,0},{0,10}};
    auto problem=Board({Original("triangle",triangle,2,{0,180})},11,11);
    for(const auto kind:{PlaneStrategyKind::NfpClosest,PlaneStrategyKind::NfpDirectional,PlaneStrategyKind::NfpAllSpecifications})
    {
        const auto result=MakePlaneStrategyFactory(Constructor(kind))()->Run(problem,Budget());
        const auto checked=Validate(problem,result);
        Require(checked.Valid&&checked.Complete&&result.Layouts.size()==1,"NFP missed a necessary sloped common-edge placement");
        const auto& placed=result.Layouts[0].Placements;
        Require(placed.size()==2&&SharedLength(triangle.Outer,placed[0].Transform,triangle.Outer,placed[1].Transform)>14.13,
            "sloped constructor used only envelope adjacency instead of the original diagonal seam");
    }
    problem.AllowCommonEdges=false;
    const auto ordinary=MakePlaneStrategyFactory(Constructor(PlaneStrategyKind::NfpClosest))()->Run(problem,Budget());
    Require(Validate(problem,ordinary).Valid&&!Validate(problem,ordinary).Complete,"sloped common-edge exception leaked into ordinary positive-gap construction");
}

void DiagnoseNonOrthogonal(const Problem& problem,const PlaneStrategySettings& settings,
    const Result& baseline,const char* name)noexcept
{
    try
    {
        std::cerr<<std::setprecision(17)<<"DIAG nonorthogonal route="<<name<<" baseline-status="<<static_cast<int>(baseline.State)
            <<" baseline-attempts="<<baseline.Stats.PlacementAttempts<<'\n';
        const auto printLayout=[&](const char* label,const Result& result)
        {
            std::cerr<<"DIAG "<<label<<" layouts="<<result.Layouts.size()<<" remaining-types="<<result.Remaining.size()<<'\n';
            std::size_t shown=0;
            for(const auto& board:result.Layouts)for(const auto& placed:board.Placements)
                if(shown++<8)std::cerr<<"DIAG "<<label<<" part="<<placed.PartID<<" instance="<<placed.Instance
                    <<" pose=("<<placed.Transform.X<<','<<placed.Transform.Y<<','<<placed.Transform.AngleDegrees<<")\n";
        };
        printLayout("baseline",baseline);
        std::optional<Placement> first;
        for(const auto& board:baseline.Layouts)if(!board.Placements.empty()){first=board.Placements.front();break;}
        std::size_t stream2Proposals=0,stream2Rejects=0;
        std::map<std::string,std::size_t> reasons;
        struct Sample{Pose Transform;std::uint64_t Candidate=0,Proposal=0;std::string Reason;};
        std::vector<Sample> samples,fallback;
        std::vector<Pose> certifiedPhases;
        auto options=Budget();options.EnableCombinations=true;
        options.ConstructionTraceRoute=name;options.ConstructionTraceScope="nonorthogonal-failure-diagnostic";
        options.ConstructionObservation=std::make_shared<ConstructionTraceHealth>();
        options.ConstructionTrace=[&](const ConstructionTraceEvent& event)
        {
            if(event.Source=="nfp.original-clearance"&&event.Reason=="first-seed-certified-common-continuation"&&
                !event.Members.empty()&&certifiedPhases.size()<3)certifiedPhases.push_back(event.Members[0].Transform);
            if(event.Stage==ConstructionTraceStage::Commit&&!event.Layouts.empty()&&!event.Layouts[0].Placements.empty())
                first=event.Layouts[0].Placements.front();
            bool stream2=false;
            for(const auto& measure:event.Measures)
                if(measure.first=="nominal-common-domain"&&measure.second==2)stream2=true;
            if(event.Source=="nfp.original-clearance"&&stream2&&event.Stage==ConstructionTraceStage::Generate)
                ++stream2Proposals;
            if(!event.Accepted)
            {
                ++reasons[event.Source+"/"+event.Reason];
                if(stream2)++stream2Rejects;
            }
            if(!stream2||event.Source!="nfp.original-clearance"||event.Members.empty()||event.Accepted)return;
            const auto pose=event.Members[0].Transform;
            const auto same=[&](const Sample& value){return value.Transform.X==pose.X&&value.Transform.Y==pose.Y&&value.Transform.AngleDegrees==pose.AngleDegrees;};
            Sample sample{pose,event.CandidateID,event.ProposalID,event.Reason};
            if(fallback.size()<3&&std::none_of(fallback.begin(),fallback.end(),same))fallback.push_back(sample);
            if(samples.size()<3&&first&&event.Reason=="original-clearance-pair-blocked"&&
                SharedLength(problem.Parts[0].Geometry.Outer,first->Transform,problem.Parts[0].Geometry.Outer,pose)>1&&
                std::none_of(samples.begin(),samples.end(),same))samples.push_back(sample);
        };
        const auto traced=MakePlaneStrategyFactory(settings)()->Run(problem,options);
        printLayout("traced",traced);
        std::cerr<<"DIAG stream2-proposals="<<stream2Proposals<<" stream2-rejected="<<stream2Rejects
            <<" trace-failed="<<options.ConstructionObservation->Failed.load()<<" omitted="<<options.ConstructionObservation->Omitted.load()<<'\n';
        if(first)std::cerr<<"DIAG actual-first pose=("<<first->Transform.X<<','<<first->Transform.Y<<','<<first->Transform.AngleDegrees<<")\n";
        for(const auto phase:certifiedPhases)std::cerr<<"DIAG actual-certified-phase first=("<<phase.X<<','<<phase.Y<<")\n";
        for(const auto& [reason,count]:reasons)std::cerr<<"DIAG rejection count="<<count<<" reason="<<reason<<'\n';
        if(samples.empty())samples=std::move(fallback);
        for(const auto& sample:samples)
        {
            std::cerr<<"DIAG sample proposal="<<sample.Proposal<<" candidate="<<sample.Candidate<<" reason="<<sample.Reason
                <<" pose=("<<sample.Transform.X<<','<<sample.Transform.Y<<','<<sample.Transform.AngleDegrees<<')';
            if(first)
            {
                const auto a=AxisSafety::Make(problem.Parts[0].Geometry,first->Transform);
                const auto b=AxisSafety::Make(problem.Parts[0].Geometry,sample.Transform);
                bool common=false;
                try{common=AxisSafety::CommonEdge(a,b,problem.Tolerance,false);}catch(const AxisSafety::ArithmeticUncertainty&){}
                std::cerr<<" exact-common="<<common<<" pair="<<static_cast<int>(AxisSafety::Pair(a,b,problem.PartGap,true,problem.Tolerance))
                    <<" pair-exact="<<static_cast<int>(AxisSafety::PairExact(a,b,problem.PartGap,true,problem.Tolerance))
                    <<" visually-shared-length="<<SharedLength(problem.Parts[0].Geometry.Outer,first->Transform,problem.Parts[0].Geometry.Outer,sample.Transform);
            }
            std::cerr<<'\n';
        }
    }
    catch(...){std::cerr<<"DIAG nonorthogonal diagnostic failed without changing the original assertion\n";}
}

void NonOrthogonalSharedEdge()
{
    constexpr double angle=23.7,side=3;
    constexpr double radians=3.14159265358979323846/180;
    const double c=std::cos(angle*radians),s=std::sin(angle*radians);
    // The common-edge pair is a 6x3 rectangle turned by 23.7 degrees. The
    // usable board is its world-axis envelope, with only a tiny roundoff
    // allowance. Identically oriented squares can move at most ~3 along their
    // local X and ~2.21 along local Y, so an ordinary .2 gap cannot fit here.
    // Every original still has only the non-orthogonal source angle available.
    auto problem=Board({Original("oblique-square",Rectangle(side,side),2,{angle})},
        2*side*c+side*s+1+2e-5,2*side*s+side*c+1+2e-5);
    auto options=Budget();options.EnableCombinations=true;
    for(const auto& [kind,name]:std::vector<std::pair<PlaneStrategyKind,const char*>>{
        {PlaneStrategyKind::NfpClosest,"nfp-closest"},
        {PlaneStrategyKind::ArrayMultiSpecificationSearch,"array-search"}})
    {
        const auto settings=Constructor(kind);
        const auto result=MakePlaneStrategyFactory(settings)()->Run(problem,options);
        const auto checked=Validate(problem,result);
        if(!(checked.Valid&&checked.Complete&&result.Layouts.size()==1))DiagnoseNonOrthogonal(problem,settings,result,name);
        Require(checked.Valid&&checked.Complete&&result.Layouts.size()==1,
            std::string(name)+" missed common-edge construction at the actual 23.7-degree angle");
        const auto& placed=result.Layouts[0].Placements;
        Require(placed.size()==2,std::string(name)+" non-orthogonal construction lost an original instance");
        for(const auto& original:placed)
            Require(std::abs(original.Transform.AngleDegrees-angle)<1e-10,
                std::string(name)+" replaced the only allowed 23.7-degree angle with an orthogonal angle");
        Require(SharedLength(problem.Parts[0].Geometry.Outer,placed[0].Transform,
            problem.Parts[0].Geometry.Outer,placed[1].Transform)>side-1e-6,
            std::string(name)+" non-orthogonal result lacks a real full-length shared straight edge");
        const auto first=AxisSafety::Make(problem.Parts[0].Geometry,placed[0].Transform);
        const auto second=AxisSafety::Make(problem.Parts[0].Geometry,placed[1].Transform);
        Require(AxisSafety::CommonEdge(first,second,problem.Tolerance,false)&&
            AxisSafety::Pair(first,second,problem.PartGap,true,problem.Tolerance)==AxisSafety::Verdict::Safe&&
            AxisSafety::PairExact(first,second,problem.PartGap,true,problem.Tolerance)==AxisSafety::Verdict::Safe,
            std::string(name)+" final output coordinates lack an exact shared edge or strict material separation");

        auto ordinaryProblem=problem;ordinaryProblem.AllowCommonEdges=false;
        const auto ordinary=MakePlaneStrategyFactory(settings)()->Run(ordinaryProblem,options);
        const auto ordinaryCheck=Validate(ordinaryProblem,ordinary);
        Require(ordinaryCheck.Valid&&!ordinaryCheck.Complete&&ordinaryCheck.PlacedCount==1,
            std::string(name)+" waived the ordinary .2 gap for a 23.7-degree pair when common edges were disabled");
    }
}

void HoleBoundaryAndOrdinaryCircleGap()
{
    auto host=Rectangle(10,10);host.Holes={{{3,3},{7,3},{7,7},{3,7}}};
    auto problem=Board({Original("frame",host),Original("insert",Rectangle(4,4))},11,11);
    for(const auto kind:{PlaneStrategyKind::NfpClosest,PlaneStrategyKind::ArrayQuantityDeferred,PlaneStrategyKind::ArrayMultiSpecificationSearch})
    {
        const auto result=MakePlaneStrategyFactory(Constructor(kind))()->Run(problem,Budget());
        const auto checked=Validate(problem,result);
        Require(checked.Valid&&checked.Complete&&result.Layouts.size()==1,"constructor missed a true exact-fit hole with common edges");
        const auto& placed=result.Layouts[0].Placements;
        const auto frame=std::find_if(placed.begin(),placed.end(),[](const auto& value){return value.PartID=="frame";});
        const auto insert=std::find_if(placed.begin(),placed.end(),[](const auto& value){return value.PartID=="insert";});
        Require(frame!=placed.end()&&insert!=placed.end()&&SharedLength(host.Holes[0],frame->Transform,problem.Parts[1].Geometry.Outer,insert->Transform)>15.99,
            "hole construction did not retain the actual four-sided inner seam");
    }
    problem.AllowCommonEdges=false;
    const auto separated=MakePlaneStrategyFactory(Constructor(PlaneStrategyKind::NfpClosest))()->Run(problem,Budget());
    Require(Validate(problem,separated).Valid&&!Validate(problem,separated).Complete,"hole construction spent the required ordinary positive gap");

    Shape disk;disk.Radius=1;disk.Center={0,0};
    const auto circles=Board({Original("circle",disk,2)},5.4,3);
    const auto roundResult=MakePlaneStrategyFactory(Constructor(PlaneStrategyKind::NfpClosest))()->Run(circles,Budget());
    Require(Validate(circles,roundResult).Valid&&Validate(circles,roundResult).Complete,"common-edge candidate generation lost ordinary circle clearance");
    const auto& placed=roundResult.Layouts[0].Placements;
    Require(placed.size()==2&&std::hypot(placed[0].Transform.X-placed[1].Transform.X,placed[0].Transform.Y-placed[1].Transform.Y)>=2.2-1e-8,
        "a curved tangency was allowed to replace the ordinary .2 gap");
}
}

int main()
{
    int passed=0,failed=0;
    const auto run=[&](const std::string& name,const auto& action)
    {
        try{action();++passed;std::cout<<"PASS "<<name<<'\n';}
        catch(const std::exception& error){++failed;std::cerr<<"FAIL "<<name<<": "<<error.what()<<'\n';}
    };
    for(const auto& [kind,name]:std::vector<std::pair<PlaneStrategyKind,const char*>>{
        {PlaneStrategyKind::ArrayQuantityDeferred,"array-quantity"},
        {PlaneStrategyKind::ArrayGeometryDeferred,"array-geometry"},
        {PlaneStrategyKind::ArrayMultiSpecificationSearch,"array-search"},
        {PlaneStrategyKind::NfpClosest,"nfp-closest"},
        {PlaneStrategyKind::NfpDirectional,"nfp-directional"},
        {PlaneStrategyKind::NfpAllSpecifications,"nfp-all-specifications"}})
        run(name,[&]{RectanglesAndTrace(kind,name,0);RectanglesAndTrace(kind,name,90);});
    run("sloped original seam",SlopedSharedEdge);
    run("non-orthogonal original common edge",NonOrthogonalSharedEdge);
    run("inner-hole seam and ordinary circle gap",HoleBoundaryAndOrdinaryCircleGap);
    std::cout<<"CommonEdgeConstructionTests: "<<passed<<" passed, "<<failed<<" failed\n";
    return failed?1:0;
}
