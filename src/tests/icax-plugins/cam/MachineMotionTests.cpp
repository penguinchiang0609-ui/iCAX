#include "../../../iCAX-Plugins/cam/Machine/MachineMotion.h"
#include "../../../iCAX-Plugins/cam/Machine/MachineSweep.h"
#include <cassert>
#include <iostream>
using namespace iCAX::CAM::Motion;
int main(){
    std::vector<Node> nodes(7);nodes[0].ID="base";nodes[1].Parent=0;nodes[1].ID="table";
    for(int i=2;i<5;++i){nodes[i].Parent=i==2?0:i-1;nodes[i].Type="prismatic";nodes[i].Axis={0,0,0};nodes[i].Axis[i-2]=1;nodes[i].Lower=-1000;nodes[i].Upper=1000;}
    nodes[5].Parent=4;nodes[5].Type="revolute";nodes[5].Axis={1,0,0};nodes[5].Lower=-3.141592653589793;nodes[5].Upper=3.141592653589793;
    nodes[6].Parent=5;nodes[6].Type="revolute";nodes[6].Axis={0,1,0};nodes[6].Lower=-3.141592653589793;nodes[6].Upper=3.141592653589793;
    Target target;target.Support=1;target.Tool=6;target.TCP={0,0,-80};target.Position={130,70,25};target.Direction=Unit({.3,.4,-1});
    auto s=Solve(nodes,target,std::vector<double>(7));assert(s.Reached&&s.PositionError<.01&&s.DirectionError<1e-5);
    target.Position={4000,0,0};assert(!Solve(nodes,target,s.Positions).Reached);
    target.Position={20,30,40};target.Direction={0,0,1};assert(Solve(nodes,target,std::vector<double>(7)).Reached);
    // Workpiece on a rotary table, fixed downward tool: the relative tool pose
    // must change when the support moves, not just when the tool chain moves.
    nodes[1].Type="revolute";nodes[1].Axis={1,0,0};nodes[1].Lower=-2;nodes[1].Upper=2;
    nodes[5].Type=nodes[6].Type="fixed";target.Direction=Unit({0,.5,-1});
    s=Solve(nodes,target,std::vector<double>(7));assert(s.Reached&&std::abs(s.Positions[1])>.1);
    nodes[2].Origin={0,-1,0,25,1,0,0,10,0,0,1,0,0,0,0,1};
    s=Solve(nodes,target,std::vector<double>(7));assert(s.Reached);
    // Endpoints are clear while the articulated sweep crosses an obstacle.
    const auto collision=CertifySweep({-10},{10},20,.1,[](const auto& q){return std::max(0.,std::abs(q[0])-1);});
    assert(!collision.Clear&&collision.Fraction>0&&collision.Fraction<1);
    const auto clear=CertifySweep({-10},{10},20,.1,[](const auto& q){return std::hypot(q[0],5.)-1;});assert(clear.Clear);
    std::vector<Node> arm(2);arm[0].Type="revolute";arm[0].Axis={0,0,1};arm[1].Parent=0;arm[1].Origin[3]=100;
    const std::vector<double> qa{0,0},qb{3.141592653589793,0};const auto bound=SweepBound(arm,1,10,qa,qb);
    assert(bound>=110*3.141592653589793-1e-8);
    arm.push_back(arm[1]);arm[2].Origin[3]=-100;
    assert(RelativeSweepBound(arm,1,10,2,10,{0,0,0},{3.14,0,0})==0);
    arm[1].Type="prismatic";arm[1].Axis={1,0,0};
    assert(RelativeSweepBound(arm,1,10,2,10,{0,0,0},{3.14,12,0})==12);
    const auto stationary=CertifySweep({0},{1},0,.1,[](const auto&){return .2;});assert(stationary.Clear&&stationary.Queries==1);
    std::cout<<"PASS: five-axis pose, travel rejection, opposite axis, rotary support, rotated origin, interior collision, certified sweep, rotary bound\n";
}
