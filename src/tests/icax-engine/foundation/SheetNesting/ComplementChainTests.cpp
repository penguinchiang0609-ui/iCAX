#include "NestingInternal.h"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <map>
#include <stdexcept>

using namespace iCAX::SheetNesting;
namespace
{
    void Require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
    Problem Nested(std::size_t families,std::size_t copies,double shear,double angle=0,bool subdivide=false,bool materials=false)
    {
        Problem problem;const double width=160,height=120;
        const double radians=-angle*Detail::Pi/180,c=std::cos(radians),s=std::sin(radians);
        for(std::size_t f=0;f<families;++f)
        {
            double x=0,y=0;
            for(std::size_t k=0;k<4;++k)
            {
                Part part;part.ID="part-"+std::to_string((f*4+k)*37+11);part.Quantity=copies;
                part.FreeRotation=false;part.AllowedAnglesDegrees={angle};part.Material=materials?(f%2?"steel":"aluminium"):"";
                if(k<3)
                {
                    const double nx=x+(width-x)*(.28+.035*((f+2*k)%5)),ny=y+(height-y)*(.32+.025*((f+k)%4));
                    part.Geometry.Outer={{x,y},{width,y},{width,ny},{nx,ny},{nx,height},{x,height}};x=nx;y=ny;
                }
                else part.Geometry.Outer={{x,y},{width,y},{width,height},{x,height}};
                if(subdivide)
                {
                    std::vector<Point> points;
                    for(std::size_t j=0;j<part.Geometry.Outer.size();++j)
                    {
                        const auto a=part.Geometry.Outer[j],b=part.Geometry.Outer[(j+1)%part.Geometry.Outer.size()];
                        points.push_back(a);points.push_back({(a.X+b.X)/2,(a.Y+b.Y)/2});
                    }
                    part.Geometry.Outer=std::move(points);
                }
                const double shiftX=static_cast<double>(static_cast<int>((f*7+k*3)%23)-11),shiftY=static_cast<double>(static_cast<int>((f*3+k*11)%29)-14);
                for(auto& point:part.Geometry.Outer)
                {
                    const double worldX=point.X+shear*point.Y,worldY=point.Y;
                    point={c*worldX-s*worldY+shiftX,s*worldX+c*worldY+shiftY};
                }
                problem.Parts.push_back(std::move(part));
            }
        }
        if(materials)
        {
            problem.Stocks={{"s-steel",width+std::abs(shear)*height,height,families*copies,1,"steel"},
                {"s-aluminium",width+std::abs(shear)*height,height,families*copies,1,"aluminium"}};
        }
        else problem.Stocks={{"stock",width+std::abs(shear)*height,height,families*copies,1,""}};
        std::rotate(problem.Parts.begin(),problem.Parts.begin()+static_cast<std::ptrdiff_t>(problem.Parts.size()/3),problem.Parts.end());
        return problem;
    }
    Options Budget()
    {
        Options options;options.TimeLimitSeconds=5;options.MaxPlacementAttempts=200000;
        // This test isolates complete basic-unit placement; generic gap filling
        // belongs to the other jobs and must not conceal a missing four-part unit.
        options.EnableGapFill=false;return options;
    }
    JobResult Solve(const Problem& problem,const Options& options)
    {return RunJob({"complement-test","rules.complement-chains",problem,options},DefaultStrategies());}
    std::size_t Singletons(const std::vector<Detail::Pattern>& patterns)
    {return std::count_if(patterns.begin(),patterns.end(),[](const auto& p){return p.Members.size()==1;});}
}
int main()
{
    try
    {
        for(double shear:{0.0,.25,-.4})
        {
            const auto p=Nested(8,2,shear,0,true);
            const auto patterns=Detail::BuildComplementChains(p,Budget(),{});
            auto singleOptions=Budget();singleOptions.EnableCombinations=false;
            Require(Singletons(patterns)==Detail::BuildPatterns(p,singleOptions,{}).size(),
                "same-original chain policy discarded ordinary original singleton directions");
            for(const auto& pattern:patterns)
            {
                Require(!pattern.Members.empty(),"complement chain lost its original member");
                for(const auto& member:pattern.Members)
                    Require(member.PartIndex==pattern.Members.front().PartIndex,
                        "nested geometric families crossed the original PartIndex boundary");
            }
        }
        for(const auto& source:{Nested(12,3,.4),Nested(4,2,.25,30,true,true)})
        {
            auto singleOptions=Budget();singleOptions.EnableCombinations=false;
            const auto patterns=Detail::BuildComplementChains(source,Budget(),{});
            Require(Singletons(patterns)==Detail::BuildPatterns(source,singleOptions,{}).size(),
                "large or transformed inventory lost an original singleton direction");
            for(const auto& pattern:patterns)for(const auto& member:pattern.Members)
                Require(member.PartIndex==pattern.Members.front().PartIndex,
                    "large/transformed complement chain preassembled different originals");
        }
        for(const bool identicalContours:{false,true})
        {
            Problem ordinary;Part first;first.ID="ordinary-A";first.Quantity=identicalContours?1:2;
            first.FreeRotation=false;first.AllowedAnglesDegrees={0};
            first.Geometry.Outer={{0,0},{4,0},{4,2},{0,2}};
            auto second=first;second.ID="ordinary-B";
            if(!identicalContours)second.Geometry.Outer={{0,0},{2,0},{2,2},{0,2}};
            ordinary.Parts={first,second};ordinary.Stocks={{"ordinary",identicalContours?8.:12.,2,1}};
            const auto solved=Solve(ordinary,Budget());
            Require(solved.Checked.Valid&&solved.Checked.Complete&&solved.Output.Layouts.size()==1,
                "self-combination restriction prevented normal different-original placement on the same sheet");
            std::map<std::size_t,std::string> groups;
            for(const auto& placement:solved.Output.Layouts.front().Placements)if(placement.Group)
            {
                const auto [same,inserted]=groups.emplace(placement.Group,placement.PartID);
                Require(inserted||same->second==placement.PartID,
                    "ordinary same-sheet placement silently created a different-original object");
            }
        }
        std::cout<<"PASS same-original discovery boundaries, retained 64/144-piece source directions and normal mixed-sheet placement\n";
        auto p=Nested(2,1,0);auto options=Budget();auto baseline=options;baseline.EnableCombinations=false;
        const auto base=Detail::BuildPatterns(p,baseline,{});const auto baseCount=base.size();
        for(int control=0;control<3;++control)
        {
            auto controlled=p;auto o=options;
            if(control==0)o.EnableCombinations=false;
            if(control==1)controlled.PartGap=1;
            if(control==2)o.MaxPatternMembers=3;
            const auto patterns=Detail::BuildComplementChains(controlled,o,{});
            Require(Singletons(patterns)==baseCount,"rule controls must preserve every original singleton direction");
            for(const auto& pattern:patterns)
            {
                Require(pattern.Members.size()<4,"controls must not publish a complete four-member chain");
                if(control!=2)Require(pattern.Members.size()==1,"disabled combinations or positive gap must never be presented as a solid union");
            }
        }
        auto absent=p;
        for(auto& part:absent.Parts)if(part.Geometry.Outer.size()==4)part.Quantity=0;
        for(const auto& pattern:Detail::BuildComplementChains(absent,options,{}))
            Require(pattern.Members.size()==1,"a missing leaf inventory must not be replaced by a fabricated piece");
        auto forbidden=Nested(2,1,0,30);
        for(auto& part:forbidden.Parts)part.AllowedAnglesDegrees={0};
        for(const auto& pattern:Detail::BuildComplementChains(forbidden,options,{}))
            Require(pattern.Members.size()==1,"an unavailable physical direction must not be invented by a rule");
        auto capped=p;
        for(auto& part:capped.Parts)part.AllowedAnglesDegrees={0,90};
        auto capOptions=options;capOptions.MaxPatterns=1;
        const auto cappedPatterns=Detail::BuildComplementChains(capped,capOptions,{});
        Require(Singletons(cappedPatterns)==2*capped.Parts.size(),"a one-combination cap must retain later specifications and their required 90-degree directions");
        auto mismatched=p;mismatched.Parts.front().Material="unavailable-material";
        for(const auto& pattern:Detail::BuildComplementChains(mismatched,options,{}))
            for(const auto& member:pattern.Members)
                Require(mismatched.Parts[member.PartIndex].Material==mismatched.Parts[pattern.Members.front().PartIndex].Material,"a chain must not combine incompatible materials");
        std::cout<<"PASS disabled/gap/depth controls, leaf inventory, forbidden directions, material mismatch and one-combination cap\n";
        std::size_t cuts=0;const auto budgetProblem=Nested(4,2,.25);
        for(std::size_t attempts:{1,5,20,100,1000})
        {
            auto o=Budget();o.MaxPlacementAttempts=attempts;const auto r=Solve(budgetProblem,o);
            Require(r.Checked.Valid&&r.Output.Stats.PlacementAttempts<=attempts,"attempt limits must conserve all original quantities");++cuts;
        }
        for(std::size_t cut:{1,7,20,50,200})
        {
            std::size_t calls=0;auto o=Budget();o.Cancel=[&]{return ++calls==cut;};const auto r=Solve(budgetProblem,o);
            Require(r.Checked.Valid&&r.Output.State==Status::Cancelled,"one-shot cancellation must retain legal completed placements and explicit remaining demand");++cuts;
        }
        std::cout<<"PASS "<<cuts<<" attempt and one-shot cancellation cuts\n";return 0;
    }
    catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
}
