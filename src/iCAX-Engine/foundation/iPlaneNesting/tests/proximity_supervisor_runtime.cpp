#include "ordinary_supervisor.hpp"
#include "ordinary_unplaced.hpp"
#include <iostream>

int main(){using namespace study;try{
    size_t cases=0,placements=0,pointRows=0;
    for(const bool x:{false,true})for(const int manager:{4,10,20,0}){
        Input input;input.width=x?6.:5.;input.height=x?5.:6.;input.pitch=.25;
        PartInput first,second;first.quantity=second.quantity=1;first.poses=second.poses={{0,false}};
        first.polygon=x?Polygon{{0,0},{2,0},{2,3},{0,3}}:Polygon{{0,0},{3,0},{3,2},{0,2}};
        second.polygon=x?Polygon{{0,0},{1,0},{1,3},{0,3}}:Polygon{{0,0},{3,0},{3,1},{0,1}};
        input.parts={first,second};PointsProximityGroup group;group.axis=x?ProximityAxis::X:ProximityAxis::Y;
        group.endpoints={ProximityEndpoint{0,0,x?Point{2,1.5}:Point{1.5,2}},
                         ProximityEndpoint{1,0,x?Point{0,1.5}:Point{1.5,0}}};
        input.pointGroups={group};SupervisorProgram program;
        if(manager){program.initialPreset=manager==4?1000:manager==10?1000000000:0;
            program.reversePreset=manager==20?1000000000:0;
            program.selectionTrialLimit=1;program.optimizationTrialLimit=0;
            program.strategyFraction=1.;program.usePostOptimization=false;}
        SupervisorTrace trace;Budget budget(manager?3.:1.);
        const auto output=ordinaryDefault(input,budget,program,&trace);
        require(output.placements.size()==2&&valid_layout(input,output)&&validPointsProximity(input,output,input.pointGroups),
                PN2S_INTERNAL,"Supervisor ordinary, specialized, reverse and mixed strategies must preserve real point constraints");
        std::array<Point,2> points;
        for(size_t side=0;side<2;++side){const auto &endpoint=group.endpoints[side];
            const auto found=std::find_if(output.placements.begin(),output.placements.end(),[&](const auto &p){return p.group==endpoint.group&&p.ordinal==endpoint.ordinal;});
            require(found!=output.placements.end(),PN2S_INTERNAL,"Point constraint lost an endpoint instance");
            const auto &part=input.parts[endpoint.group];auto point=transform(Polygon{endpoint.point},part.pivot,part.poses.at(found->poseIndex)).front();
            point.x+=double(found->x)/1e6;point.y+=double(found->y)/1000.;points[side]=point;}
        require(std::abs(x?points[0].x-points[1].x:points[0].y-points[1].y)<=1e-8,
                PN2S_INTERNAL,"Independent world point coordinates violate the declared axial relation");
        if(manager)require(trace.managerAttempts.size()==1&&trace.managerAttempts.contains(manager)&&trace.unsupportedManagers.empty(),
                           PN2S_INTERNAL,"Constraint supervisor must execute the actual selected family");
        else require(trace.postOptimizationTasks>0&&trace.postOptimizationPointRows>0&&
                     !trace.missingProviders.contains("supervisor.pointProximityShakerConstraints"),PN2S_INTERNAL,
                     "Constrained supervisor optimization must actually emit the recovered hard point rows");
        ++cases;placements+=output.placements.size();pointRows+=trace.postOptimizationPointRows;
    }
    std::cout<<"{\"pass\":true,\"cases\":"<<cases<<",\"placements\":"<<placements<<",\"actualPointRows\":"<<pointRows<<"}\n";
}catch(const std::exception &error){std::cerr<<error.what()<<'\n';return 1;}}
