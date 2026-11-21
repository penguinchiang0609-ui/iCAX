#pragma once
#include <cstddef>
#include <optional>
#include <set>

namespace iCAX::SheetNesting::Detail
{
    enum class SheetCountOutcome { Unknown, InfeasibleOnLattice, Complete };
    struct SheetCountPlan
    {
        std::size_t Boards=0;
        bool Revisit=false;
        std::size_t PendingInRound=1;
        std::size_t Round=0;
    };

    // Deterministic scheduling only: no geometry, task IDs or placement hints.
    // A checked incumbent changes the upper bound but never erases unresolved
    // smaller counts. Remaining time can therefore improve a complete result.
    class SheetCountSchedule
    {
        const std::size_t Lower,Upper;
        std::size_t NextInitial,Round=0;
        std::optional<std::size_t> Best;
        std::set<std::size_t> Unknown,VisitedThisRound;
    public:
        SheetCountSchedule(std::size_t lower,std::size_t upper):Lower(lower),Upper(upper),NextInitial(lower){}
        std::optional<std::size_t> Incumbent()const{return Best;}
        std::size_t UnresolvedCount()const{return Unknown.size();}
        bool AtAreaLowerBound()const{return Best&&*Best==Lower;}
        std::optional<SheetCountPlan> Next(bool budgetAvailable=true,bool cancelled=false)
        {
            if(!budgetAvailable||cancelled||AtAreaLowerBound())return std::nullopt;
            if(!Best&&NextInitial<=Upper)return SheetCountPlan{NextInitial++,false,1,0};
            if(Unknown.empty())return std::nullopt;
            auto first=Unknown.end();std::size_t pending=0;
            for(auto k:Unknown)if(!VisitedThisRound.contains(k))
            {if(first==Unknown.end())first=Unknown.find(k);++pending;}
            if(first==Unknown.end())
            {VisitedThisRound.clear();++Round;first=Unknown.begin();pending=Unknown.size();}
            else if(!Round)++Round;
            const auto k=*first;VisitedThisRound.insert(k);return SheetCountPlan{k,true,pending,Round};
        }
        void Observe(const SheetCountPlan& plan,SheetCountOutcome outcome)
        {
            if(outcome==SheetCountOutcome::Complete)
            {
                if(!Best||plan.Boards<*Best)Best=plan.Boards;
                for(auto it=Unknown.begin();it!=Unknown.end();)
                    if(*it>=*Best)it=Unknown.erase(it);else ++it;
            }
            else if(outcome==SheetCountOutcome::InfeasibleOnLattice)Unknown.erase(plan.Boards);
            else if(!Best||plan.Boards<*Best)Unknown.insert(plan.Boards);
        }
    };
}
