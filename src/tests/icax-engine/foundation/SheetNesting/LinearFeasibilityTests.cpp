#include "NestingLinearFeasibility.h"
#include "LinearFeasibilityRegressions.h"
#include <iostream>
#include <stdexcept>
using namespace iCAX::SheetNesting::Detail;
static void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
static bool satisfies(const std::vector<LinearRow>& rows,const std::vector<double>& point,double tolerance=1e-9)
{
    for(const auto x:point)if(!std::isfinite(x)||x<0)return false;
    for(const auto& row:rows)
    {
        if(row.A.size()!=point.size())return false;long double lhs=0;
        for(std::size_t j=0;j<point.size();++j)lhs+=static_cast<long double>(row.A[j])*point[j];
        if(lhs-row.B>tolerance)return false;
    }
    return true;
}
int main()try
{
    for(const auto& regression:{iCAX::SheetNesting::TestData::PhaseOneRegression519(),iCAX::SheetNesting::TestData::PointRegression47()})
    {
        require(satisfies(regression.Rows,regression.Witness),"regression witness is not valid");
        std::size_t work=0;std::vector<double> point;LinearFeasibility lp(regression.Variables,regression.Rows,work,[&]{return work>=25000;});
        const auto state=lp.solve(regression.Variables,point,true);
        require(state!=LinearState::Infeasible,"feasible original regression falsely pruned");
        if(state==LinearState::Feasible)require(satisfies(regression.Rows,point),"unchecked numerical regression point returned");
        require(work<=25000,"numerical regression exceeded shared work budget");
    }
    std::vector<LinearRow> feasible{{{1,0},1},{{0,1},1},{{-1,0},-.25},{{1,1},1.5}};
    std::size_t work=0;std::vector<double> point;LinearFeasibility lp(2,feasible,work,[] {return false;});
    require(lp.solve(2,point,true)==LinearState::Feasible&&satisfies(feasible,point),"negative-RHS phase-I feasible case failed");
    for(const auto& rows:std::vector<std::vector<LinearRow>>{
        {{{1},.25},{{-1},-.5}},{{{0},-1}},{{{1},-.1}},
        {{{1,0},1},{{0,1},1},{{1,1},1},{{-1,-1},-2}}})
    {
        work=0;LinearFeasibility impossible(rows.front().A.size(),rows,work,[] {return false;});
        require(impossible.solve(rows.front().A.size(),point,true)==LinearState::Infeasible,"simple infeasibility certificate not found");
    }
    // The unbounded deviation variables must not contaminate a position-only
    // infeasibility certificate or the extracted nonnegative position point.
    std::vector<LinearRow> l1{{{1,0,0,0},1},{{0,1,0,0},1},{{-1,-1,0,0},-1.2},
        {{1,0,-1,0},.2},{{-1,0,-1,0},-.2},{{0,1,0,-1},.2},{{0,-1,0,-1},-.2}};
    work=0;LinearFeasibility l1Solver(4,l1,work,[] {return false;});
    require(l1Solver.solve(2,point)==LinearState::Feasible&&point.size()==2,"L1 phase-II failed");
    require(point[0]>=0&&point[1]>=0&&std::abs(std::abs(point[0]-.2)+std::abs(point[1]-.2)-.8)<1e-8,"L1 optimum incorrect");
    auto full=point;full.push_back(std::abs(point[0]-.2));full.push_back(std::abs(point[1]-.2));require(satisfies(l1,full),"L1 point violates original rows");
    auto impossibleL1=l1;impossibleL1[2].B=-3;work=0;LinearFeasibility infeasibleL1(4,impossibleL1,work,[] {return false;});
    require(infeasibleL1.solve(2,point)==LinearState::Infeasible,"unbounded deviations defeated valid position certificate");
    work=0;LinearFeasibility cancelled(2,feasible,work,[] {return true;});
    require(cancelled.solve(2,point,true)==LinearState::Limited&&point.empty()&&work==0,"cancellation leaked a result");
    work=0;LinearFeasibility bounded(4,l1,work,[&] {return work>=1;});const auto limited=bounded.solve(2,point);
    require(work<=1&&limited==LinearState::Limited&&point.empty(),"pivot work limit not honoured");
    std::vector<LinearRow> malformed{{{1},1}};work=0;LinearFeasibility invalid(2,malformed,work,[] {return false;});
    require(invalid.solve(2,point,true)==LinearState::Limited,"malformed matrix misclassified");
    std::cout<<"Linear feasibility original-row checks, certificates, phase-I/II, regressions and limits passed\n";
}
catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
