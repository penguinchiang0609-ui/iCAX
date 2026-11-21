#include "ordinary_escher.hpp"
#include "ordinary_pair_map.hpp"
#include "clipper2/clipper.h"
#include <iostream>
#include <set>
using namespace study;
namespace {
namespace clip=Clipper2Lib;
clip::PathsD material(const Input &input,const Placement &placement) {
    const auto &part=input.parts.at(placement.group);
    auto contours=ordinaryPreparedContours(part,part.poses.at(placement.poseIndex));
    clip::PathsD paths;for(const auto &ring:contours){clip::PathD path;for(const auto p:ring)path.push_back({p.x+double(placement.x)/1e6,p.y+double(placement.y)/1000.});paths.push_back(std::move(path));}return paths;
}
double clippedArea(const clip::PathsD &paths){return std::abs(clip::Area(paths));}
void independentGeometry(const Input &input,const Output &output) {
    std::vector<clip::PathsD> parts;clip::PathD sheetRing;
    for(const Point p:std::array<Point,4>{{{0,0},{input.width,0},{input.width,input.height},{0,input.height}}})sheetRing.emplace_back(p.x,p.y);
    const clip::PathsD sheet{sheetRing};
    for(const auto &placement:output.placements){auto paths=material(input,placement);
        require(clippedArea(clip::Difference(paths,sheet,clip::FillRule::NonZero,8))<1e-8,PN2S_INTERNAL,"Escher hole material crosses stock boundary");
        for(const auto &old:parts)require(clippedArea(clip::Intersect(paths,old,clip::FillRule::NonZero,8))<1e-8,PN2S_INTERNAL,"Escher hole material overlap");parts.push_back(std::move(paths));}
}
PartInput frame(size_t scene) {
    PartInput part;part.polygon={{0,0},{6,0},{6,6},{0,6}};
    if(scene%3==0)part.holes={{{1,1},{5,1},{5,5},{1,5}}};
    else if(scene%3==1)part.holes={{{1,1},{3,1},{3,4},{1,4}},{{4,2},{5,2},{5,5},{4,5}}};
    else part.holes={{{.5,1},{4.5,1},{4.5,4},{.5,4}}};
    if(scene%2)for(auto &ring:part.holes)std::reverse(ring.begin(),ring.end());
    part.poses={{0,false},{180,false},{0,true},{180,true}};part.quantity=12;
    if(scene>=2){for(auto &point:part.polygon){point.x+=10;point.y+=20;}for(auto &ring:part.holes)for(auto &point:ring){point.x+=10;point.y+=20;}part.pivot={12,23};}
    return part;
}
size_t holeOccupants(const Input &input,const Output &output) {
    size_t inside=0;
    for(const auto &small:output.placements)if(small.group==1){const auto geometry=material(input,small);bool found=false;
        for(const auto &large:output.placements)if(large.group==0){const auto &part=input.parts[0];for(const auto &hole:part.holes){auto ring=transform(hole,part.pivot,part.poses.at(large.poseIndex));clip::PathD path;for(const auto p:ring)path.push_back({p.x+double(large.x)/1e6,p.y+double(large.y)/1000.});if(clip::Area(path)<0)std::reverse(path.begin(),path.end());
                if(clippedArea(clip::Difference(geometry,clip::PathsD{path},clip::FillRule::NonZero,8))<1e-8){found=true;break;}}if(found)break;}
        inside+=found;
    }
    return inside;
}
}
int main()try {
    size_t cases=0,placements=0,patternCommitted=0,integerHoles=0,boundaryOwners=0,reflectedWarm=0,reflectedProvider=0,fillersInHoles=0;
    for(size_t scene=0;scene<8;++scene) {
        Input input;input.width=24;input.height=18;input.pitch=.25;input.randomSeed=uint32_t(29+scene);input.parts.push_back(frame(scene));
        if(scene==6)input.parts[0].poses={{0,true},{180,true}};
        if(scene==7){auto &part=input.parts[0];part.polygon={{0,0},{6,0},{6,2},{2,2},{2,6},{0,6}};part.holes={{{.5,.5},{1.5,.5},{1.5,1.5},{.5,1.5}}};part.pivot={1.5,2.};}
        if(scene==0||scene==3){PartInput filler;filler.polygon={{0,0},{1.5,0},{1.5,1.5},{0,1.5}};filler.poses={{0,false},{180,false}};filler.quantity=12;input.parts.push_back(filler);}
        require(requires_general(input)&&ordinaryPreparedDomain(input),PN2S_INTERNAL,"Escher fixture must exercise the newly supported clean-hole domain");
        Budget budget(40.);auto program=escherProgram(input,10.);program.trial13bc=1;program.fallback90=input.parts.size()>1||scene>=4;OrdinaryState state;
        recovered::WarmPlacementPtr warm;Placement saved;
        if(scene==4||scene==5){OrdinaryControl initial;initial.placementLimit=0;for(const auto &part:input.parts)initial.values.push_back(material_area(part));ordinaryRun(input,budget,state,&initial);
            const auto box=bounds(transform(input.parts[0].polygon,input.parts[0].pivot,input.parts[0].poses[3]));saved={0,0,3,rounded(-box.x0*1e6),int32_t(rounded(-box.y0*1000.))};ordinaryCommit(state,saved);warm=state.placementHost->nesting.placements.front();}
        const auto before=state.output.placements.size();OrdinaryEscherTrace trace;const auto added=ordinaryEscherContinue(input,program,budget,state,&trace);
        require(added==int32_t(state.output.placements.size()-before),PN2S_INTERNAL,"Escher hole count differs from actual catalog commits");
        require(trace.integerOuterContours==input.parts.size()&&trace.integerHoleContours==input.parts[0].holes.size(),PN2S_INTERNAL,"Escher integer owner lost an outer contour or signed hole");
        require(trace.providers>0&&trace.patternRequests>0&&trace.rawPatterns>0&&trace.committed>0&&trace.boundaryHoleOwners>0,PN2S_INTERNAL,"Hole scene did not traverse real Escher owner/provider/boundary and pattern commit");
        require(trace.missingProviders.empty(),PN2S_INTERNAL,"Escher hole scene used an unavailable provider");
        require(valid_layout(input,state.output),PN2S_INTERNAL,"Actual Escher multi-ring layout validator failed");independentGeometry(input,state.output);
        double requiredArea=0,actualArea=0;std::set<std::pair<size_t,size_t>> identities;
        for(size_t group=0;group<input.parts.size();++group){requiredArea+=material_area(input.parts[group])*input.parts[group].quantity;size_t count=0;for(const auto &p:state.output.placements)count+=p.group==group;
            const auto &catalog=*state.catalog;const auto &record=catalog.records.at(catalog.groups[group].front());require(record.nestedCount==int32_t(count),PN2S_INTERNAL,"Escher hole live catalog disagrees with output");
            require(count==size_t(input.parts[group].quantity),PN2S_INTERNAL,"Simple Escher hole scene left required instances unplaced");}
        for(size_t n=0;n<state.output.placements.size();++n){const auto &p=state.output.placements[n];require(identities.emplace(p.group,p.ordinal).second,PN2S_INTERNAL,"Escher hole scene duplicates an instance");actualArea+=material_area(input.parts[p.group]);
            require(std::abs(state.placementHost->nesting.placements[n]->area58-material_area(input.parts[p.group]))<1e-9,PN2S_INTERNAL,"Escher placement owner counts hole void as material");}
        require(std::abs(state.placementHost->requiredArea-requiredArea)<1e-8&&std::abs(state.placementHost->informerArea-actualArea)<1e-8,PN2S_INTERNAL,"Escher live material area includes hole void");
        if(warm){const auto &now=state.output.placements.front();require(state.placementHost->nesting.placements.front()==warm&&now.group==saved.group&&now.ordinal==saved.ordinal&&now.poseIndex==saved.poseIndex&&now.x==saved.x&&now.y==saved.y,PN2S_INTERNAL,"Escher altered its reflected, pivoted warm placement");++reflectedWarm;}
        if(scene==6){require(trace.committed==12,PN2S_INTERNAL,"Reflected-only symmetric frame did not commit its actual provider batch");for(const auto &p:state.output.placements){require(input.parts[p.group].poses[p.poseIndex].flip,PN2S_INTERNAL,"Escher selected a forbidden unreflected pose");++reflectedProvider;}}
        const auto filled=holeOccupants(input,state.output);if(input.parts.size()>1)require(filled>0,PN2S_INTERNAL,"Ordinary continuation never placed a filler in an actual frame hole");
        ++cases;placements+=state.output.placements.size();patternCommitted+=trace.committed;integerHoles+=trace.integerHoleContours;boundaryOwners+=trace.boundaryHoleOwners;fillersInHoles+=filled;
        std::cout<<"{\"scene\":"<<scene<<",\"placements\":"<<state.output.placements.size()<<",\"patternCommitted\":"<<trace.committed<<",\"integerHoles\":"<<trace.integerHoleContours<<",\"boundaryHoleOwners\":"<<trace.boundaryHoleOwners<<",\"fillersInHoles\":"<<filled<<",\"rejectedPatterns\":"<<trace.rejectedPatterns<<"}\n";
    }
    std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"placements\":"<<placements<<",\"patternCommitted\":"<<patternCommitted<<",\"integerHoles\":"<<integerHoles<<",\"boundaryHoleOwners\":"<<boundaryOwners<<",\"reflectedWarm\":"<<reflectedWarm<<",\"reflectedProvider\":"<<reflectedProvider<<",\"fillersInHoles\":"<<fillersInHoles<<"}\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}
