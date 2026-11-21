#include "ordinary_runtime.hpp"
#include "recovered_part_geometry.hpp"
#include <iostream>
namespace {
study::Polygon comb(){study::Polygon result{{0,0},{184,0},{184,6}};for(int tooth=89;tooth>=0;--tooth){const double x=2.+tooth*2.;result.push_back({x+1.,6});result.push_back({x+1.,3});result.push_back({x,3});result.push_back({x,6});}result.push_back({0,6});return result;}
}
int main(){using namespace study;try{
 size_t cases=0,placements=0,queries=0,warmCalls=0,eligibilityCalls=0;
 for(const bool shifted:{false,true})for(const bool directional:{false,true}){
  Input input;input.width=400;input.height=24;input.pitch=2.;
  PartInput complex;complex.polygon=comb();complex.quantity=2;complex.poses={{0}};
  PartInput simple;simple.polygon={{0,0},{3,0},{3,2},{0,2}};simple.quantity=2;simple.poses={{0}};
  if(shifted){for(auto *part:{&complex,&simple}){for(auto &point:part->polygon){point.x+=7;point.y-=3;}part->pivot={8,-2};}}
  validate_polygon(complex.polygon);validate_polygon(simple.polygon);input.parts={complex,simple};
  const std::array<Polygon,1> complexRings{complex.polygon},simpleRings{simple.polygon};
  require(recovered::partGeometryTooComplex(recovered::partGeometryStatistics(complexRings))&&!recovered::partGeometryTooComplex(recovered::partGeometryStatistics(simpleRings)),PN2S_INTERNAL,"Runtime input must straddle the actual original5316a0 default threshold");
  std::vector<recovered::EligibilityPart> eligibility;for(const auto &part:input.parts){const auto box=bounds(part.polygon);recovered::EligibilityPart record;record.minX=box.x0;record.minY=box.y0;record.maxX=box.x1;record.maxY=box.y1;eligibility.push_back(record);}
  recovered::MapEligibilityState mapState;recovered::MapEligibilityInput mapInput;mapInput.parts=eligibility;mapInput.typeLimit188=200;mapInput.problemScale=2;mapInput.pitch230=2000000;mapInput.memoryLimitD0=8000;
  OrdinaryState state;state.mapEligible=[&]{++eligibilityCalls;return recovered::mapEligible(mapState,mapInput);};
  OrdinaryControl control;control.values={1,1};control.referenceGeometry=Polygon{{0,0},{4,0},{4,2},{0,2}};control.extremaCount=4;control.extremaMaxSteps=2;control.placementLimit=2;
  if(directional)control.directionVectors={{-1.,-1.},{-1.,0.},{0.,-1.},{1.,1.}};
  Budget budget(60.);const auto first=ordinaryRun(input,budget,state,&control);require(first==2&&state.output.placements.size()==2&&valid_layout(input,state.output),PN2S_INTERNAL,"Complexity NoMap first stage must commit two real legal placements");
  require(mapState.status==2&&state.noMapCandidateQueries>0,PN2S_INTERNAL,"NoMap must be selected by actual geometry complexity while real Context Map eligibility remains true");
  const auto before=state.output.placements;const auto second=ordinaryRun(input,budget,state,&control);require(second==2&&state.output.placements.size()==4&&state.noMapCommitted==2&&mapState.status==2&&valid_layout(input,state.output),PN2S_INTERNAL,"Mixed complexity NoMap/ordinary Map warm continuation must commit both complex pieces and fill the actual request legally");
  for(size_t n=0;n<before.size();++n)require(before[n].group==state.output.placements[n].group&&before[n].x==state.output.placements[n].x&&before[n].y==state.output.placements[n].y,PN2S_INTERNAL,"Warm append must preserve existing accepted placements");
  ++cases;placements+=state.output.placements.size();queries+=state.noMapCandidateQueries;++warmCalls;
 }
 // A normal low-complexity request with the same real eligibility provider
 // must retain the established Map path and avoid creating a NoMap provider.
 {Input input;input.width=20;input.height=10;input.pitch=1.;PartInput part;part.polygon={{0,0},{3,0},{3,2},{0,2}};part.quantity=3;part.poses={{0}};input.parts={part};OrdinaryState state;recovered::MapEligibilityState mapState{2,-1};recovered::MapEligibilityInput mapInput;state.mapEligible=[&]{return recovered::mapEligible(mapState,mapInput);};OrdinaryControl control;control.values={1};control.referenceGeometry=part.polygon;Budget budget(10.);ordinaryRun(input,budget,state,&control);require(state.output.placements.size()==3&&state.noMapCandidateQueries==0&&state.noMapCommitted==0&&valid_layout(input,state.output),PN2S_INTERNAL,"Normal low-complexity Map result must remain on its existing provider");++cases;placements+=state.output.placements.size();}
 std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"placements\":"<<placements<<",\"complexityCandidateQueries\":"<<queries<<",\"warmCalls\":"<<warmCalls<<",\"eligibilityCalls\":"<<eligibilityCalls<<"}\n";
 }catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
