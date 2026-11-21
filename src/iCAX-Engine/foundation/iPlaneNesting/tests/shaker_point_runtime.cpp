#include "ordinary_shaker_constraints.hpp"
#include "ordinary_unplaced.hpp"
#include "shaker_runtime.hpp"
#include "recovered_rich_conversion.hpp"
#include <bit>
#include <iostream>
namespace {
size_t checks=0,cases=0,solves=0,rows=0,iterations=0;
void check(bool value,const char *message){++checks;if(!value)throw std::runtime_error(message);}
study::Output outputFrom(const study::Output &source,const study::recovered::ShakerInputScene &scene){auto output=source;for(size_t n=0;n<output.placements.size();++n){auto &p=output.placements[n];p.x=study::rounded(scene.parts[n].position[0]*1e6);p.y=study::rounded(scene.parts[n].position[1]*1000.);p.exactX=scene.parts[n].position[0];p.exactY=scene.parts[n].position[1];}return output;}
}
int main(){using namespace study;using namespace study::recovered;try{
 for(int quarter=0;quarter<4;++quarter)for(bool reflected:{false,true}){
    Input input;input.width=16;input.height=16;input.parts.resize(2);const Pose pose{double(quarter*90),reflected};
    for(auto &part:input.parts){part.polygon={{5,-4},{7,-4},{7,-2},{5,-2}};part.pivot={5,-4};part.poses={pose};part.quantity=1;}
    PointsProximityGroup x;x.axis=ProximityAxis::X;x.tolerance=reflected?.35:0.;x.endpoints={ProximityEndpoint{0,0,{7,-3}},ProximityEndpoint{1,0,{5,-3}}};auto y=x;y.axis=ProximityAxis::Y;input.pointGroups={x,y};
    const auto localFirst=transform(study::Polygon{x.endpoints[0].point},input.parts[0].pivot,pose).front(),localSecond=transform(study::Polygon{x.endpoints[1].point},input.parts[1].pivot,pose).front();Point delta{localFirst.x-localSecond.x,localFirst.y-localSecond.y};
    if(reflected){if(std::abs(delta.x)>1){delta.x+=delta.x<0?-.2:.2;delta.y+=.1;}else{delta.y+=delta.y<0?-.2:.2;delta.x+=.1;}}
    Output source;source.placements={{0,0,0,8000000,8000},{1,0,0,rounded((8+delta.x)*1e6),rounded((8+delta.y)*1000.)}};
    check(valid_layout(input,source)&&validPointsProximity(input,source,input.pointGroups),"Real point scene starts with valid rotated/reflected source geometry and point intervals");
    ShakerInputScene scene;shakerConfigureOrdinaryRelations(scene.relations);ConvolutionCurves curves;
    for(size_t n=0;n<2;++n){const auto &part=input.parts[n];ShakerPolygonInput polygon;for(auto point:part.polygon)polygon.outer.push_back({point.x,point.y});const auto geometry=shakerCreateGeometry(scene,{part.pivot.x,part.pivot.y});shakerAppendPolygonComponents(scene,geometry,polygon,curves);const auto partIndex=shakerCreatePart(scene,geometry,{double(source.placements[n].x)/1e6,double(source.placements[n].y)/1000.},pose.angle*pi/180.,{uint8_t(pose.flip),0});const auto box=bounds(transform(part.polygon,part.pivot,pose));shakerSetMobility(scene.parts[partIndex],{0,0,1},{-box.x0,input.width-box.x1,-box.y0,input.height-box.y1},{pose.angle*pi/180.,pose.angle*pi/180.});}
    shakerCreateBoundaryGeometry(scene,64,64,16);check(shakerSetBounds(scene,0,16,0,16)==0,"Real point scene boundary creation");const auto constraints=ordinaryShakerPointConstraints(input,source);check(constraints.size()==4,"Two axial point pairs compile to four native hard constraint records");
    const auto run=compactShakerFixedScene(scene,0,2,4.,0,constraints);check(run.status==0&&run.iteration.success&&run.solves>2&&run.iteration.iterations>2,"Real compact point scene performs multiple accepted optimization steps");check(run.constraintRows==4*run.solves,"Every real LP build emits the four hard point rows");const auto first=outputFrom(source,scene);check(valid_layout(input,first)&&validPointsProximity(input,first,input.pointGroups),"Multiple actual LP updates preserve rotated/reflected world-point intervals");check(source.placements[0].x+source.placements[1].x-first.placements[0].x-first.placements[1].x>1000000,"Actual scene moves farther than one translation window, proving multiple accepted improvements");
    for(size_t n=0;n<2;++n)check(scene.parts[n].reference50==ShakerPoint{double(source.placements[n].x)/1e6,double(source.placements[n].y)/1000.},"reference50 remains the original pose through every LP refresh and saved-pose writeback");
    const auto again=compactShakerFixedScene(scene,3,1,4.,0,constraints);const auto second=outputFrom(source,scene);check(again.status==0&&again.iteration.success&&again.constraintRows==4*again.solves,"A second compact call with different directions consumes the same correct constraint coordinate frame");check(valid_layout(input,second)&&validPointsProximity(input,second,input.pointGroups),"Repeated multidirection compaction keeps real geometry and point intervals valid");for(size_t n=0;n<2;++n)check(scene.parts[n].reference50==ShakerPoint{double(source.placements[n].x)/1e6,double(source.placements[n].y)/1000.},"Different compact objectives do not rescale or replace initial reference50");
    solves+=run.solves+again.solves;rows+=run.constraintRows+again.constraintRows;iterations+=size_t(run.iteration.iterations+again.iteration.iterations);++cases;
 }
 std::cout<<"{\"pass\":true,\"sceneCases\":"<<cases<<",\"compactions\":"<<cases*2<<",\"solves\":"<<solves<<",\"constraintRows\":"<<rows<<",\"iterations\":"<<iterations<<",\"checks\":"<<checks<<"}\n";return 0;
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
