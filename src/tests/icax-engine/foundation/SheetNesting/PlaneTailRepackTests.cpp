#include "SheetNestingPlaneStrategies.h"
#include "NestingAxisSafety.h"
#include <algorithm>
#include <iostream>
#include <map>
#include <set>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
Part Rectangle(const char* id,double width,double height,std::size_t quantity)
{
    Part part;part.ID=id;part.Quantity=quantity;part.FreeRotation=false;part.AllowedAnglesDegrees={0,90};
    part.Geometry.Outer={{0,0},{width,0},{width,height},{0,height}};return part;
}
Options Limits(bool fill)
{
    Options options;options.TimeLimitSeconds=10;options.MaxPlacementAttempts=2000000;
    options.EnableCombinations=false;options.MaxPatternMembers=1;options.EnableGapFill=fill;
    options.EnableBasicCompaction=false;return options;
}
Result Run(const Problem& problem,PlaneStrategyKind kind,bool fill)
{
    PlaneStrategySettings settings;settings.Kind=kind;settings.EnableLayoutRefinement=true;
    auto output=MakePlaneStrategyFactory(settings)()->Run(problem,Limits(fill));
    Require(Validate(problem,output).Valid,"tail reconstruction violated original demand or geometry");return output;
}
}
int main()
{
    try
    {
        Problem room;room.Parts={Rectangle("small",2,2,8),Rectangle("large",4,4,1)};
        room.Stocks={{"sheet",8,6,1}};
        auto baseline=Run(room,PlaneStrategyKind::NfpArray,false);
        Require(baseline.State==Status::Partial&&baseline.Layouts[0].Placements.size()==8,"fixture needs tail reconstruction after the array blocks the last large original");
        auto improved=Run(room,PlaneStrategyKind::NfpArray,true);
        Require(improved.State==Status::Complete&&improved.Layouts[0].Placements.size()==9,"releasing at most six tail members should make room for another original");
        for(std::size_t instance:{0u,1u})
        {
            const auto& old=baseline.Layouts[0].Placements[instance];
            const auto found=std::find_if(improved.Layouts[0].Placements.begin(),improved.Layouts[0].Placements.end(),[&](const Placement& q){return q.PartID==old.PartID&&q.Instance==old.Instance;});
            Require(found!=improved.Layouts[0].Placements.end()&&found->Transform.X==old.Transform.X&&found->Transform.Y==old.Transform.Y&&found->Transform.AngleDegrees==old.Transform.AngleDegrees&&found->Group==old.Group,"unreleased prefix originals must remain fixed");
        }
        Require(improved.Stats.GapFilledParts==1,"recovered array members must retain their original filler provenance");

        Problem positive;positive.Parts={Rectangle("bars",5,2,2),Rectangle("block",4,4,1)};
        positive.Stocks={{"sheet",9.5,6.8,1}};positive.PartGap=.2;positive.EdgeMargin=.5;
        auto before=Run(positive,PlaneStrategyKind::NfpArray,false);
        Require(before.State==Status::Partial,"positive-gap fixture should need tail reordering");
        for(auto kind:{PlaneStrategyKind::NfpArray,PlaneStrategyKind::NfpMultiArray})
        {
            auto after=Run(positive,kind,true);Require(after.State==Status::Complete,"positive-gap tail reordering should complete the original set");
            const auto& members=after.Layouts[0].Placements;std::map<std::string,const Part*> parts;
            for(const auto& p:positive.Parts)parts[p.ID]=&p;
            for(std::size_t i=0;i<members.size();++i)
            {
                const auto& q=members[i];const auto geometry=AxisSafety::Make(parts.at(q.PartID)->Geometry,q.Transform);
                Require(AxisSafety::Containment(geometry,9.5,6.8,.5)==AxisSafety::Verdict::Safe,"tail reordering lost the original edge margin");
                for(std::size_t j=0;j<i;++j)Require(AxisSafety::Pair(geometry,AxisSafety::Make(parts.at(members[j].PartID)->Geometry,members[j].Transform),.2,false,positive.Tolerance)==AxisSafety::Verdict::Safe,"tail reordering lost strict original clearance");
            }
        }

        auto cancelled=Limits(true);cancelled.Cancel=[]{return true;};
        PlaneStrategySettings settings;settings.Kind=PlaneStrategyKind::NfpArray;settings.EnableLayoutRefinement=true;
        auto stopped=MakePlaneStrategyFactory(settings)()->Run(positive,cancelled);
        Require(stopped.State==Status::Cancelled&&stopped.Layouts.empty()&&Validate(positive,stopped).Valid,"cancelled tail-capable construction must keep intact remaining demand");
        auto limited=Limits(true);limited.MaxPlacementAttempts=30;
        auto bounded=MakePlaneStrategyFactory(settings)()->Run(positive,limited);
        Require(Validate(positive,bounded).Valid&&bounded.Stats.PlacementAttempts<=30,"tail trials must share the constructor attempt cap");
        std::cout<<"PlaneTailRepackTests passed: bounded release, original identity and fixed-prefix retention, filler provenance, positive gaps and cancellation\n";return 0;
    }
    catch(const std::exception& error){std::cerr<<"PlaneTailRepackTests failed: "<<error.what()<<'\n';return 1;}
}
