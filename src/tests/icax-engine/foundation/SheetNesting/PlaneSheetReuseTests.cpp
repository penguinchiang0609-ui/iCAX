#include "SheetNestingPlaneStrategies.h"
#include <algorithm>
#include <iomanip>
#include <iostream>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* message)
{if(!value)throw std::runtime_error(message);}

Problem RepeatedPairs(std::size_t lowerQuantity=12,std::size_t upperQuantity=12,std::size_t stockQuantity=100)
{
    Problem problem;problem.PartGap=.01;problem.EdgeMargin=.01;
    Part lower;lower.ID="lower-original";lower.Material="alloy";lower.Quantity=lowerQuantity;
    lower.FreeRotation=false;lower.AllowedAnglesDegrees={0};lower.Geometry.Outer={{0,0},{4,0},{0,4}};
    auto upper=lower;upper.ID="upper-original";upper.Quantity=upperQuantity;
    upper.Geometry.Outer={{4,4},{0,4},{4,0}};
    problem.Parts={lower,upper};problem.Stocks={{"plate",4.05,4.05,stockQuantity,1,"alloy"}};return problem;
}
Options Limits()
{
    Options options;options.TimeLimitSeconds=8;options.MaxPlacementAttempts=2000000;
    options.MaxPatternMembers=2;options.MaxPatterns=256;options.EnableBasicCompaction=false;
    return options;
}
Result Construct(const Problem& problem,PlaneStrategyKind kind,bool reuse=true,double minimum=.85,Options options=Limits())
{
    PlaneStrategySettings settings;settings.Kind=kind;settings.MaxNodes=1;
    settings.EnableContourSettling=false;settings.EnableAllowedAngleSettling=false;
    settings.EnableSheetTemplateReuse=reuse;settings.MinimumSheetReuseUtilization=minimum;
    const auto output=MakePlaneStrategyFactory(settings)()->Run(problem,options);
    Require(Validate(problem,output).Valid,"board reuse must retain original geometry, identity and inventory");
    Require(output.Stats.PlacementAttempts<=options.MaxPlacementAttempts,"board reuse exceeded the shared attempt cap");return output;
}
std::size_t Copies(const Result& output)
{
    for(auto it=output.Diagnostics.rbegin();it!=output.Diagnostics.rend();++it)
    {
        if(it->find("plane.sheet-template-reuse:")!=0)continue;
        const auto start=it->find("copies=");Require(start!=std::string::npos,"reuse report omitted its physical copy count");
        return static_cast<std::size_t>(std::stoull(it->substr(start+7)));
    }
    return 0;
}
void Inventory(const Problem& problem,const Result& output)
{
    std::map<std::string,std::set<std::size_t>> originals,stocks;
    std::map<std::size_t,std::size_t> groupOwner;
    for(std::size_t bi=0;bi<output.Layouts.size();++bi)
    {
        const auto& board=output.Layouts[bi];
        Require(stocks[board.StockID].insert(board.StockInstance).second,"copy reused a physical stock instance");
        for(const auto& member:board.Placements)
        {
            Require(originals[member.PartID].insert(member.Instance).second,"copy reused an original part instance");
            Require(member.Transform.AngleDegrees==0,"copy changed the original allowed orientation");
            if(member.Group)
            {
                const auto [where,inserted]=groupOwner.emplace(member.Group,bi);
                Require(inserted||where->second==bi,"copy failed to remap a positive group on a new board");
            }
        }
    }
    std::map<std::string,std::size_t> remaining;
    for(const auto& row:output.Remaining)
        Require(remaining.emplace(row.PartID,row.Quantity).second,"residual inventory contains duplicate original rows");
    for(const auto& part:problem.Parts)
    {
        const auto& ids=originals[part.ID];std::size_t next=0;
        for(const auto id:ids)Require(id==next++,"physical copies left a hole in original instance numbering");
        Require(ids.size()+remaining[part.ID]==part.Quantity,"copy or tail construction over/under-produced an original specification");
    }
    for(const auto& [id,ids]:stocks)
    {
        (void)id;std::size_t next=0;
        for(const auto instance:ids)Require(instance==next++,"physical copy stock numbering is not contiguous");
    }
}
std::string Recipe(const Layout& board)
{
    std::vector<const Placement*> members;
    for(const auto& member:board.Placements)members.push_back(&member);
    std::stable_sort(members.begin(),members.end(),[](const auto* a,const auto* b){return a->PartID<b->PartID;});
    std::map<std::size_t,std::size_t> groups;std::size_t nextGroup=1;
    std::ostringstream text;text<<std::setprecision(17);
    for(const auto* member:members)
    {
        std::size_t group=0;
        if(member->Group)
        {
            const auto [where,inserted]=groups.emplace(member->Group,nextGroup);
            if(inserted)++nextGroup;group=where->second;
        }
        text<<member->PartID<<':'<<member->Transform.X<<':'<<member->Transform.Y<<':'
            <<member->Transform.AngleDegrees<<':'<<group<<':'<<member->GapFiller<<';';
    }
    return text.str();
}
}

int main()try
{
    for(const auto kind:{PlaneStrategyKind::ArrayMultiSpecificationSearch,PlaneStrategyKind::NfpArray,
        PlaneStrategyKind::ArrayCompetition,PlaneStrategyKind::MixedCompetition})
    {
        const auto problem=RepeatedPairs();
        const auto reused=Construct(problem,kind),independent=Construct(problem,kind,false);
        Require(Validate(problem,reused).Complete&&Validate(problem,independent).Complete,
            "both reuse and independent construction must supply the entire repeated order");
        Require(reused.Layouts.size()==12&&independent.Layouts.size()==12,"complementary pairs should use one physical board per pair");
        Require(Copies(reused)==11,"constructor did not expand eleven additional copies of its current-run mixed board");
        const auto recipe=Recipe(reused.Layouts.front());
        for(const auto& board:reused.Layouts)
        {
            Require(board.Placements.size()==2&&Recipe(board)==recipe,"copied board changed geometry, grouping or filler provenance");
            Require(board.MaterialArea==16,"copy used enclosing-box area instead of original material area");
        }
        Inventory(problem,reused);Inventory(problem,independent);
        std::cout<<"reuse kind="<<static_cast<int>(kind)<<" constructed="<<reused.Layouts.size()-Copies(reused)
            <<" copied="<<Copies(reused)<<" parts="<<Validate(problem,reused).PlacedCount
            <<" attempts="<<reused.Stats.PlacementAttempts<<" independent-attempts="<<independent.Stats.PlacementAttempts<<'\n';
    }

    const auto uneven=RepeatedPairs(13,12);
    const auto tail=Construct(uneven,PlaneStrategyKind::ArrayMultiSpecificationSearch);
    Require(Validate(uneven,tail).Complete&&tail.Layouts.size()==13&&Copies(tail)==11,
        "nonmultiple inventory must rebuild its final original instead of rounding up a whole recipe");
    Require(tail.Layouts.back().Placements.size()==1,"nonmultiple remainder did not receive a separately constructed tail board");
    Inventory(uneven,tail);

    const auto limitedStock=RepeatedPairs(12,12,5);
    const auto partial=Construct(limitedStock,PlaneStrategyKind::ArrayMultiSpecificationSearch);
    Require(partial.State==Status::Partial&&partial.Layouts.size()==5&&Copies(partial)==4,
        "whole-board copies exceeded remaining physical stock or recounted their first board");
    Inventory(limitedStock,partial);
    Require(partial.Remaining.size()==2&&partial.Remaining[0].Quantity==7&&partial.Remaining[1].Quantity==7,
        "stock-limited copying lost the exact outstanding specification counts");

    const auto problem=RepeatedPairs();
    const auto gated=Construct(problem,PlaneStrategyKind::ArrayMultiSpecificationSearch,true,1.);
    Require(Validate(problem,gated).Complete&&gated.Layouts.size()==12&&Copies(gated)==0,
        "density gate must decline physical copies while retaining ordinary construction");Inventory(problem,gated);
    auto geometricCapacity=RepeatedPairs();geometricCapacity.Stocks[0].Width=geometricCapacity.Stocks[0].Height=4.5;
    PlaneStrategySettings defaults;defaults.Kind=PlaneStrategyKind::ArrayMultiSpecificationSearch;
    defaults.EnableContourSettling=false;
    const auto attainable=MakePlaneStrategyFactory(defaults)()->Run(geometricCapacity,Limits());
    Require(Validate(geometricCapacity,attainable).Complete&&attainable.Layouts.size()==12&&Copies(attainable)==11,
        "default reuse must not impose an arbitrary fill gate on a board that can geometrically hold only one complementary pair");
    Inventory(geometricCapacity,attainable);
    const auto unique=RepeatedPairs(1,1);
    const auto once=Construct(unique,PlaneStrategyKind::ArrayMultiSpecificationSearch);
    Require(Validate(unique,once).Complete&&once.Layouts.size()==1&&Copies(once)==0,
        "one-off originals cannot form additional complete physical board copies");Inventory(unique,once);

    auto cancelled=Limits();cancelled.Cancel=[]{return true;};
    const auto stopped=Construct(problem,PlaneStrategyKind::ArrayMultiSpecificationSearch,true,.85,cancelled);
    Require(stopped.State==Status::Cancelled&&stopped.Layouts.empty()&&stopped.Remaining.size()==2,
        "cancelled reuse construction must preserve all demand");Inventory(problem,stopped);
    for(std::size_t cap:{1u,10u,100u})
    {
        auto options=Limits();options.MaxPlacementAttempts=cap;
        const auto bounded=Construct(problem,PlaneStrategyKind::ArrayMultiSpecificationSearch,true,.85,options);Inventory(problem,bounded);
    }

    Problem diverse;diverse.PartGap=.01;diverse.EdgeMargin=.01;
    const auto seed=RepeatedPairs(80,80);
    for(std::size_t i=0;i<6;++i)for(const auto& original:seed.Parts)
    {auto part=original;part.ID=original.ID+'-'+std::to_string(i);diverse.Parts.push_back(std::move(part));}
    diverse.Stocks={{"many-specifications",4.05,4.05,600,1,"alloy"}};
    const auto batch=Construct(diverse,PlaneStrategyKind::ArrayMultiSpecificationSearch);
    Require(Validate(diverse,batch).Complete&&Copies(batch)>0,"many original specifications must remain distinct while reusable board recipes are expanded");
    Inventory(diverse,batch);
    std::cout<<"many-specifications="<<diverse.Parts.size()<<" boards="<<batch.Layouts.size()
        <<" constructed="<<batch.Layouts.size()-Copies(batch)<<" copied="<<Copies(batch)
        <<" parts="<<Validate(diverse,batch).PlacedCount<<" attempts="<<batch.Stats.PlacementAttempts<<'\n';
    std::cout<<"PlaneSheetReuseTests passed: mixed physical-board reuse, exact original inventory, tail rebuild, stock and quality gates\n";
    return 0;
}
catch(const std::exception& error){std::cerr<<"PlaneSheetReuseTests failed: "<<error.what()<<'\n';return 1;}
