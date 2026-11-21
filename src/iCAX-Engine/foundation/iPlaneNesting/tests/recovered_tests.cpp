#include "recovered_offset.hpp"
#include "recovered_catalog.hpp"
#include <iostream>
using namespace study;
using namespace study::recovered;
int main() {
    size_t checks = 0;
    auto check = [&](bool v) { if (!v) throw std::runtime_error("Recovered contract " + std::to_string(checks)); ++checks; };
    try {
        for (size_t n = 1; n <= 5; ++n) for (size_t occupied = 0; occupied < (1ULL << n); ++occupied)
            for (size_t eligible = 0; eligible < (1ULL << n); ++eligible) for (size_t start = 0; start < n; ++start) {
                Catalog c({{0}}, {static_cast<int>(n)});
                for (size_t i = 0; i < n; ++i) if (occupied & (1ULL << i)) c.setPlaced(i, true);
                c.records[0].cursor = start;
                std::optional<size_t> expected;
                for (size_t j = 0; j < n; ++j) {
                    size_t i = (start + j) % n;
                    if (!(occupied & (1ULL << i)) && (eligible & (1ULL << i))) { expected = i; break; }
                }
                auto a = c.available({0}, 1, [&](size_t i, size_t) { return (eligible & (1ULL << i)) != 0; });
                check(a.size() == (expected ? 1 : 0));
                check(c.records[0].cursor == expected.value_or(start));
                if (expected) check(a.front().instance == *expected);
            }
        Catalog c({{10, 11}, {20}, {}}, {3, 1, 0});
        auto snapshot = c;
        check(c.available({1, 0}, 1).front().module == 20);
        check(c.available({0, 0}, 1).size() == 4);
        check(c.available({9, 9, 9}, 1).size() == 3); // full-directory path ignores keys
        check(c.available({}, 1).empty());
        c.setPlaced(0, true);check(c.available({0, 1, 2}, 1)[0].instance == 1);
        c.setPlaced(0, false);check(c.available({0, 1, 2}, 1)[0].instance == 1); // cursor does not retreat
        c = snapshot;check(c.available({0, 1, 2}, 1)[0].instance == 0);
        std::vector<CandidateRef> v{{0,0},{1,0},{2,0},{3,0}};shuffle(v,1);
        check(v[0].module==1&&v[1].module==0&&v[2].module==3&&v[3].module==2);
        check(selectReference(.15)==ReferenceKind::Triangle&&selectReference(.30)==ReferenceKind::Normal);
        std::vector<double> draws{.2,.4,.6,.8,.1};size_t cursor=0;
        auto complexity=drawComplexity([&]{return draws.at(cursor++);});
        check(cursor==5&&complexity.perimeter==.4*.05&&complexity.alpha==.6*.3&&complexity.gamma==.8*.3&&complexity.useBBox);
        draws={.5,.2,.3,.2};cursor=0;complexity=drawComplexity([&]{return draws.at(cursor++);});
        check(cursor==4&&complexity.perimeter==0&&!complexity.useBBox);
        ValuePart p;p.area=200;p.quantity=2;p.variantSizes={{10,20}};
        ValuePart q;q.area=300;q.quantity=1;q.variantSizes={{30,10}};
        auto mean=effectiveAverage({p,q},700,{1,1});check(mean.x==50./3&&mean.y==110./7);
        check(effectiveAverage({},0,{7,8}).x==7);
        auto tri=referenceShape({80,40},120,ReferenceKind::Triangle);check(tri.size()==3&&tri[1].x==30&&tri[1].y==-5);
        auto wide=referenceShape({80,40},10,ReferenceKind::Wide);check(wide[0].y==-2.5&&wide[2].y==2.5);
        ScoreState state;state.goalEnabled=true;state.narrowEnabled=true;
        initialize({},state);check(state.goalEnabled&&!state.narrowEnabled);
        Initialization init;init.targetX=5000000;init.hasLastRecord=init.hasLastPart=true;
        init.maxPlacedX=4000000;init.lastMaxWidth=1;init.lastMinWidth=2;init.pitchX=1000000;
        state.narrowEnabled=true;initialize(init,state);check(!state.frontEnabled&&state.narrowEnabled&&!state.goalEnabled);
        init.lastMaxWidth=3;init.firstPositions={FirstPosition{6,2000000}};
        initialize(init,state);check(state.frontEnabled&&state.narrowEnabled&&state.limitX==8200000&&state.narrowWidth==1.8);
        init.firstPositions={FirstPosition{1,1000000}};initialize(init,state);check(!state.narrowEnabled&&state.limitX==5400000);
        check(closedCount({{{0,0}},{{2,5}}})==5);
        FrontInput f;f.sheetWidth=10;f.sheetHeight=6;f.pitchX=1000000;
        p.area=p.bboxArea=2;p.quantity=3;f.available={p};check(currentFront(f)==1);
        f.placedArea=6;f.initialCount=3000;f.currentCount=1000;f.auxMetadataHeight=2;f.targetField14b8=30;
        check(currentFront(f)==3);
        std::cout<<"{\"pass\":true,\"checks\":"<<checks<<"}\n";
    } catch(const std::exception &e) {std::cerr<<e.what()<<'\n';return 1;}
}
