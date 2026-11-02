#include <GeometryServices/MachineMotionPlanningService.h>
#include <GeometryServices/PathOrientationPlanningService.h>
#include <Services/ServiceProvider.h>
#include <Services/ServiceRegistrationCatalog.h>
#include <MachineDescriptionLoader.h>
#include "../../../iCAX-Plugins/cam/Machine/MachineMotion.h"
#include "../../../iCAX-Plugins/cam/Machine/MachineKeyposes.h"
#include <windows.h>
#include <cmath>
#include <iostream>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
namespace GS=iCAX::GeometryServices;
namespace MM=GS::MachineMotion;
namespace MK=GS::MachineKinematics;
namespace {
void Check(bool p,const char* why){if(!p)throw std::runtime_error(why);}
void Near(double a,double b,double eps,const char* why){Check(std::isfinite(a)&&std::abs(a-b)<eps,why);}
MM::Result Run(GS::IMachineMotionPlanningService& service,MM::Query query,const std::function<bool()>& cancel={}){
    // Explicit test planning limits, not a declaration about real machine dynamics.
    query.MaximumAccelerations=query.MaximumVelocities;query.MaximumJerks=query.MaximumVelocities;
    for(auto& x:query.MaximumAccelerations)x*=2;for(auto& x:query.MaximumJerks)x*=10;
    if(!query.CheckTrajectory)query.CheckTrajectory=[](const MM::AxisSegment&){return MM::Validation{MM::Verdict::Accepted,"polynomial in the explicitly empty unit-test environment"};};
    auto result=service.Plan(query,cancel);
    if(result.State==MM::Status::Solved){
        Check(result.MaximumVelocityRatio<=1&&result.MaximumAccelerationRatio<=1&&result.MaximumJerkRatio<=1,"conservative whole-segment dynamic bounds");
        const auto independent=[](const MM::AxisSegment& segment,double u,std::size_t axis,unsigned order){double value=0;for(unsigned k=order;k<6;++k){double factor=1;for(unsigned j=0;j<order;++j)factor*=k-j;value+=segment.Coefficients[axis][k]*factor*std::pow(u,int(k-order));}return value/std::pow(segment.DurationSeconds,int(order));};
        for(std::size_t k=0;k<result.Segments.size();++k){const auto& segment=result.Segments[k];for(std::size_t axis=0;axis<segment.Coefficients.size();++axis){if(query.MaximumVelocities[axis]<=0)continue;const double limits[]{0,query.MaximumVelocities[axis],query.MaximumAccelerations[axis],query.MaximumJerks[axis]};for(unsigned order=1;order<=3;++order)for(int probe=0;probe<=32;++probe)Check(std::abs(independent(segment,probe/32.,axis,order))<=limits[order]*(1+1e-7),"independent dense physical derivative check");for(unsigned order=0;order<=2;++order){const double previous=k?independent(result.Segments[k-1],1,axis,order):order?0:query.InitialPositions[axis];Near(independent(segment,0,axis,order),previous,1e-5*std::max(1.,order?limits[order]:1.),"independent C2 continuity check");}}}
    }return result;
}
MM::Query Linear(){MM::Query q;MK::Node root;root.ID="base";q.Machine.Nodes.push_back(root);MK::Node x;x.ID="X";x.Parent=0;x.Type=MK::JointType::Prismatic;x.Lower=0;x.Upper=1000;q.Machine.Nodes.push_back(x);q.TaskDefinition.ToolFrameID="X";q.TaskDefinition.ReferenceFrameID="base";q.InitialPositions={0,90};q.MaximumVelocities={0,20};q.MaximumAxisSteps={0,1000};q.IKSeedsPerTarget=1;q.IKCandidatesPerTarget=1;q.MaximumCandidatesPerStation=1;q.IKTimeMsPerTarget=500;q.MaximumSeconds=10;
    for(int i=0;i<3;++i){MM::Station s;s.Goal.PositionMm={100.+10*i,0,0};s.Goal.Direction={0,0,-1};s.ID="cut-"+std::to_string(i);s.Role=i==1?"FreeRapid":"Cut";s.FeedMmPerSecond=10;q.Stations.push_back(s);}
    // Known empty geometry in this focused test makes the complete linear edge
    // certificate exact. Product tests supply the actual frozen mesh checker.
    q.CheckState=[](auto,const auto&){return MM::Validation{MM::Verdict::Accepted,"empty test environment"};};q.CheckMotion=[](auto,const auto&,const auto&){return MM::Validation{MM::Verdict::Accepted,"complete edge in known empty environment"};};
    q.ReferenceAt=[stations=q.Stations](std::size_t edge,double u,const auto&,const auto&){MM::Reference r;r.Goal=stations.at(edge-1).Goal;for(int k=0;k<3;++k)r.Goal.PositionMm[k]+=u*(stations.at(edge).Goal.PositionMm[k]-r.Goal.PositionMm[k]);return r;};return q;}
void SparseKeyposes(GS::IMachineMotionPlanningService& service){
    namespace M=iCAX::CAM::Motion;std::vector<M::KeyposeReference> reference;
    for(int i=0;i<2720;++i)reference.push_back({{100.+20.*i/2719,0,0},{0,0,-1},false});
    auto selected=M::SelectKeyposes(reference);Check(selected==std::vector<std::size_t>{0,2719},"straight dense route uses only endpoint inverse seeds");
    auto q=Linear();q.Stations.clear();for(auto index:selected){MM::Station s;s.Goal.PositionMm=reference[index].Position;s.Goal.Direction=reference[index].Direction;s.ID="straight";s.Role="Cut";q.Stations.push_back(s);}
    q.ReferenceAt=[](std::size_t,double u,const auto&,const auto&){MM::Reference r;r.Goal.PositionMm={100.+20*u,0,0};r.Goal.Direction={0,0,-1};return r;};
    const auto result=Run(service,q);Check(result.State==MM::Status::Solved&&result.IKCalls==2,"2720 reference samples need two IK calls when interpolation meets exact reference");Check(result.MaximumPositionErrorMm<=q.PositionToleranceMm,"sparse solve retains original TCP tolerance");
    reference[713].Mandatory=true;selected=M::SelectKeyposes(reference);Check(std::find(selected.begin(),selected.end(),713)!=selected.end(),"authored pose anchor retained");
    reference.clear();for(int i=0;i<=100;++i){const double angle=2*3.141592653589793*i/100;reference.push_back({{std::cos(angle),std::sin(angle),0},{0,0,-1},false});}
    selected=M::SelectKeyposes(reference);Check(selected.size()>2&&selected.front()==0&&selected.back()==100,"small complete loop cannot collapse to a stationary endpoint");
    reference={{{0,0,0},{0,0,-1}},{{10,0,0},{0,0,-1}},{{10,10,0},{0,0,-1}}};selected=M::SelectKeyposes(reference);Check(selected.size()==3,"position corner retained");
    reference={{{0,0,0},{0,0,-1}},{{0,0,0},{1,0,0}},{{0,0,0},{0,0,-1}}};Check(M::SelectKeyposes(reference).size()==3,"direction excursion at a fixed TCP retained");
    Check(M::SelectKeyposes({}).empty(),"empty reference handled");bool invalid=false;try{M::SelectKeyposes(reference,0);}catch(const std::invalid_argument&){invalid=true;}Check(invalid,"invalid policy rejected");
}
void OptionalDynamics(GS::IMachineMotionPlanningService& service){
    const double unlimited=std::numeric_limits<double>::infinity();
    const auto make=[&]{auto q=Linear();q.MaximumAccelerations={0,unlimited};q.MaximumJerks={0,unlimited};q.MaximumSeconds=30;for(auto& s:q.Stations)s.FeedMmPerSecond=unlimited;q.CheckTrajectory=[](const MM::AxisSegment&){return MM::Validation{MM::Verdict::Accepted,"explicit empty polynomial test environment"};};return q;};
    const auto check=[&](const MM::Query& q){const auto r=service.Plan(q);if(r.State!=MM::Status::Solved)throw std::runtime_error("optional dynamics: "+r.Diagnostic);Check(r.TimingQualification=="ConfiguredAxisLimits;BlankIsUnbounded","unbounded limits are reported");
        for(std::size_t i=0;i<r.Segments.size();++i)for(unsigned d=0;d<=3;++d){const auto start=MM::Evaluate(r.Segments[i],0,d),end=MM::Evaluate(r.Segments[i],1,d);for(std::size_t axis=0;axis<start.size();++axis){Check(std::isfinite(start[axis])&&std::isfinite(end[axis]),"unbounded request produces finite polynomial derivatives");if(d<=2){const auto previous=i?MM::Evaluate(r.Segments[i-1],1,d):d?std::vector<double>(start.size(),0):q.InitialPositions;Near(start[axis],previous[axis],1e-5*std::max({1.,std::abs(start[axis]),std::abs(previous[axis])}),"C2 joins remain checked with unbounded acceleration");}if(axis&&d){const double limit=d==1?q.MaximumVelocities[axis]:d==2?q.MaximumAccelerations[axis]:q.MaximumJerks[axis];for(int sample=0;sample<=32;++sample){const auto value=MM::Evaluate(r.Segments[i],sample/32.,d)[axis];if(std::isfinite(limit))Check(std::abs(value)<=limit*(1+1e-6),"configured finite limits remain enforced");}}}}return r;};
    auto q=make();const auto unbounded=check(q);q.MaximumAccelerations[1]=.5;const auto acceleration=check(q);Check(acceleration.DurationSeconds>unbounded.DurationSeconds,"omitted acceleration cap does not acquire a fabricated finite default");
    q=make();q.MaximumJerks[1]=.25;check(q);
    q=make();MK::Node y;y.ID="Y";y.Parent=1;y.Type=MK::JointType::Prismatic;y.Axis={0,1,0};y.Lower=0;y.Upper=1000;q.Machine.Nodes.push_back(y);q.TaskDefinition.ToolFrameID="Y";q.InitialPositions.push_back(0);q.MaximumVelocities.push_back(20);q.MaximumAxisSteps.push_back(1000);q.MaximumAccelerations.push_back(1);q.MaximumJerks[1]=.5;q.MaximumJerks.push_back(unlimited);for(std::size_t i=0;i<q.Stations.size();++i)q.Stations[i].Goal.PositionMm[1]=5.*i;
    q.ReferenceAt=[stations=q.Stations](std::size_t edge,double u,const auto&,const auto&){MM::Reference r;r.Goal=stations[edge-1].Goal;for(int k=0;k<3;++k)r.Goal.PositionMm[k]+=u*(stations[edge].Goal.PositionMm[k]-r.Goal.PositionMm[k]);return r;};check(q);
    q=make();q.CheckMotion=[](auto,const auto&,const auto&){return MM::Validation{MM::Verdict::Rejected,"collision remains mandatory"};};const auto blocked=service.Plan(q);Check(blocked.State==MM::Status::Rejected&&blocked.Segments.empty(),"unbounded dynamics never bypass collisions");
    for(double invalid:{0.,-1.,std::numeric_limits<double>::quiet_NaN()}){q=make();q.MaximumAccelerations[1]=invalid;bool rejected=false;try{service.Plan(q);}catch(const std::invalid_argument&){rejected=true;}Check(rejected,"only positive finite limits or explicit unbounded values are valid");}
}
MM::Query OffsetRotary(double angle){
    MM::Query q;MK::Node root;root.ID="base";q.Machine.Nodes.push_back(root);
    for(int i=0;i<5;++i){MK::Node n;n.ID=std::array<std::string,5>{"X","Y","Z","C","A"}[i];n.Parent=i;n.Type=i<3?MK::JointType::Prismatic:MK::JointType::Revolute;n.Axis=i==0||i==4?MK::Vector{1,0,0}:i==1?MK::Vector{0,1,0}:MK::Vector{0,0,1};n.Lower=i<3?-1000:-3.141592653589793;n.Upper=-n.Lower;q.Machine.Nodes.push_back(n);}
    q.TaskDefinition.ToolFrameID="A";q.TaskDefinition.ReferenceFrameID="base";q.TaskDefinition.TCP={0,0,-220};q.TaskDefinition.Beam={0,0,-1};q.InitialPositions.resize(6,0);q.MaximumVelocities={0,100,100,100,1,1};q.MaximumAxisSteps={0,2000,2000,2000,6.3,6.3};q.PositionToleranceMm=.01;q.DirectionToleranceRad=1e-4;q.MaximumSeconds=120;
    q.Constraints={{"C",0,0,true,0}};
    for(int i=0;i<2;++i){MM::Station s;s.ID="offset-"+std::to_string(i);s.Role="Cut";s.Goal.PositionMm={100.*i,0,-220};s.Goal.Direction={0,std::sin(i*angle),-std::cos(i*angle)};s.FeedMmPerSecond=20;q.Stations.push_back(s);}
    q.ReferenceAt=[angle](std::size_t,double u,const auto&,const auto&){MM::Reference r;r.Goal.PositionMm={100*u,0,-220};r.Goal.Direction={0,std::sin(angle*u),-std::cos(angle*u)};return r;};
    q.CheckState=[](auto,const auto&){return MM::Validation{MM::Verdict::Accepted,"explicit empty state environment"};};
    // Exact minimum height along an A-axis linear submove. The discarded coarse
    // chord dips through z=-225, but the intended TCP reference stays at -220.
    q.CheckMotion=[](auto,const auto& a,const auto& b){const double da=b[5]-a[5],dz=b[3]-a[3];double minimum=std::min(a[3]-220*std::cos(a[5]),b[3]-220*std::cos(b[5]));
        if(std::abs(da)>1e-12){const auto rhs=-dz/(220*da);if(std::abs(rhs)<=1){const double phase=std::asin(rhs);for(double base:{phase,3.141592653589793-phase})for(int turn=-2;turn<=2;++turn){const auto angle=base+turn*6.283185307179586,at=(angle-a[5])/da;if(at>=0&&at<=1)minimum=std::min(minimum,a[3]+at*dz-220*std::cos(angle));}}}
        return MM::Validation{minimum>=-225?MM::Verdict::Accepted:MM::Verdict::Rejected,"complete analytic half-space collision certificate"};};return q;
}
void AdaptiveInverseSubdivision(GS::IMachineMotionPlanningService& service){
    for(double angle:{80.10607933027968*3.141592653589793/180,.09986534536977952}){
        auto q=OffsetRotary(angle);std::size_t certified=0;double largestStep=0;const auto check=q.CheckMotion;
        q.CheckMotion=[&](auto edge,const auto&a,const auto&b){if(edge){++certified;largestStep=std::max(largestStep,std::abs(b[5]-a[5]));}return check(edge,a,b);};
        const auto result=Run(service,q);if(result.State!=MM::Status::Solved)throw std::runtime_error("adaptive inverse offset route: "+result.Diagnostic);
        Check(certified>1&&largestStep<angle*.75,"coarse unsafe offset chord replaced by certified actual submoves");Check(result.IKCalls>2,"interior inverse solutions are actually computed");Check(result.MaximumPositionErrorMm<=.01&&result.MaximumDirectionErrorRad<=1e-4,"original tight TCP and direction tolerances preserved");
        Check(result.Samples.front().Positions==q.InitialPositions,"adaptive route retains actual initial axes");Near(result.Samples.back().TCP[0],100,1e-7,"adaptive final TCP X");Near(result.Samples.back().TCP[2],-220,1e-7,"adaptive final TCP height");
        double u=0,time=-1;std::size_t interiors=0;for(std::size_t i=0;i<result.Samples.size();++i){const auto&s=result.Samples[i];Check(s.TimeSeconds>=time,"adaptive timing monotonic");if(s.SourceStation==1){Check(s.SourceFraction>=u&&s.SourceFraction<=1,"original source fraction survives every interior subdivision");u=s.SourceFraction;if(u>0&&u<1)++interiors;Near(s.TCP[0],100*u,.010001,"global reference X");Near(s.TCP[1],0,.010001,"global reference Y");Near(s.TCP[2],-220,.010001,"global reference height");}
            if(i&&s.TimeSeconds>time)for(std::size_t k=1;k<s.Positions.size();++k)Check(std::abs(s.Positions[k]-result.Samples[i-1].Positions[k])/(s.TimeSeconds-time)<=q.MaximumVelocities[k]*(1+1e-7),"every emitted submove respects physical axis velocity");time=s.TimeSeconds;}
        Check(interiors>7&&u==1,"subdivision produces real executable intermediate axis positions");
        std::cout<<"  adaptive offset angle="<<angle<<" samples="<<result.Samples.size()<<" certified="<<certified<<" IK="<<result.IKCalls<<" maxTCPmm="<<result.MaximumPositionErrorMm<<'\n';
    }
    auto q=OffsetRotary(1.3981148351771546);q.CheckState=[](auto,const auto&p){return MM::Validation{p[1]>49&&p[1]<51?MM::Verdict::Rejected:MM::Verdict::Accepted,"actual midpoint obstacle"};};const auto blocked=Run(service,q);Check(blocked.State==MM::Status::Rejected&&blocked.Samples.empty(),"inserted inverse states retain collision rejection and publish no partial route");
    q=OffsetRotary(1.3981148351771546);q.MaximumVerificationDepth=1;q.MinimumVerificationDepth=1;const auto depth=Run(service,q);Check(depth.State==MM::Status::Unresolved&&depth.Samples.empty(),"adaptive inverse depth exhaustion remains explicit, no partial route");
    q=OffsetRotary(1.3981148351771546);q.MaximumVerificationSamples=64;const auto budget=Run(service,q);Check(budget.State==MM::Status::BudgetExhausted&&budget.Samples.empty(),"adaptive inverse sample budget remains hard");
    q=OffsetRotary(1.3981148351771546);bool stop=false;q.Progress=[&](const auto&stage,auto at,auto){if(stage=="Refining"&&at==1)stop=true;};const auto cancelled=Run(service,q,[&]{return stop;});Check(cancelled.State==MM::Status::Cancelled&&cancelled.Samples.empty(),"cancel while adaptive inverse refining leaves no partial plan");
    q=Linear();q.MaximumAxisSteps[1]=3;std::size_t certified=0;q.CheckMotion=[&](auto,const auto&a,const auto&b){++certified;Check(std::abs(b[1]-a[1])<=3+1e-10,"every actual submove including approach respects physical axis-step limit");return MM::Validation{MM::Verdict::Accepted,"complete empty-environment edge certificate"};};const auto stepped=Run(service,q);Check(stepped.State==MM::Status::Solved&&certified>=12,"large source gaps split into legal executable axis steps");Near(stepped.Samples.back().Positions[1],120,1e-7,"axis-step refinement retains exact final station");
}
MM::Query ActualGantryPoleWindow(){
    // Captured final automotive stations 3607..3610, after the unchanged
    // native machine placement Z=195 mm. Fixed ancestors are composed into
    // this exact X/Y/Z/C/A tool chain; physical axis limits remain unchanged.
    auto q=OffsetRotary(0);q.Constraints.clear();q.Machine.Nodes[3].Origin[11]=650;q.Machine.Nodes[5].Origin[11]=-220;
    q.Machine.Nodes[1].Lower=-1500;q.Machine.Nodes[1].Upper=1500;q.Machine.Nodes[2].Lower=-750;q.Machine.Nodes[2].Upper=750;q.Machine.Nodes[3].Lower=-450;q.Machine.Nodes[3].Upper=150;q.Machine.Nodes[5].Lower=-2.09439510239;q.Machine.Nodes[5].Upper=2.09439510239;
    q.InitialPositions={0,584.2722996315315,110,-47.8,1.5707963267948966,0};q.MaximumVelocities={0,1200,1200,800,1.5,1.5};q.MaximumAxisSteps={0,3000,1500,600,6.283185307179586,4.18879020478};
    q.Stations.clear();const std::array<MK::Vector,4> positions{{{584.2722996315315,110,162.2},{584.2722996315315,110,162.2},{582.4155572269215,110.00969612168598,162.19995299150723},{580.5588148223108,110.01939133177183,162.1998119704485}}};
    const std::array<MK::Vector,4> directions{{{0,0,-1},{0,0,-1},{0,-.009696121685975232,-.9999529915072262},{0,-.01939133177182162,-.9998119704485016}}};
    for(std::size_t i=0;i<4;++i){MM::Station station;station.ID="actual-"+std::to_string(3607+i);station.Role="Cut";station.Goal.PositionMm=positions[i];station.Goal.Direction=directions[i];station.FeedMmPerSecond=100;q.Stations.push_back(station);}
    q.ReferenceAt=[stations=q.Stations](std::size_t edge,double u,const auto&,const auto&){MM::Reference r;for(int k=0;k<3;++k)r.Goal.PositionMm[k]=stations[edge-1].Goal.PositionMm[k]*(1-u)+stations[edge].Goal.PositionMm[k]*u;
        const auto& a=stations[edge-1].Goal.Direction;const auto& b=stations[edge].Goal.Direction;const double theta=std::atan2(b[1],-b[2])-std::atan2(a[1],-a[2]),angle=std::atan2(a[1],-a[2])+theta*u;r.Goal.Direction={0,std::sin(angle),-std::cos(angle)};return r;};
    q.IKSeedsPerTarget=2;q.IKCandidatesPerTarget=2;q.MaximumCandidatesPerStation=8;q.MaximumSeconds=2;
    return q;
}
void IndependentLinearCertificateDepth(GS::IMachineMotionPlanningService& service){
    auto q=Linear();q.MinimumVerificationDepth=q.MaximumVerificationDepth=0;std::size_t certified=0;
    q.CheckMotion=[&](auto,const auto& a,const auto& b){if(std::abs(a[1]-b[1])>2)return MM::Validation{MM::Verdict::Unresolved,"finite proof allowance"};++certified;return MM::Validation{MM::Verdict::Accepted,"complete exact short-linear-edge certificate"};};
    const auto solved=Run(service,q);Check(solved.State==MM::Status::Solved&&certified>=24,"owner proof subdivision has its own finite depth even when geometry/IK depth is exhausted");Check(solved.IKCalls==3,"certificate subdivision never invokes inverse kinematics");
    const auto straight=q.ReferenceAt;q.ReferenceAt=[straight](auto edge,double u,const auto&a,const auto&b){auto r=straight(edge,u,a,b);r.Goal.PositionMm[1]+=std::sin(6.283185307179586*u);return r;};
    const auto outside=Run(service,q);Check(outside.State==MM::Status::Unresolved&&outside.Samples.empty()&&outside.IKCalls==3,"new FK probes outside hard reference tolerance reject the SAME axis curve instead of replacing it with IK");
    q=Linear();q.MinimumVerificationDepth=q.MaximumVerificationDepth=0;std::size_t calls=0;q.CheckMotion=[&](auto,const auto&,const auto&){++calls;return MM::Validation{MM::Verdict::Unresolved,"unprovable even after exact subdivision"};};const auto finite=Run(service,q);Check(finite.State==MM::Status::Unresolved&&finite.Samples.empty()&&calls<=13,"independent certificate depth is bounded and never turns unresolved into accepted");
}
void FutureSeededPoleContinuity(GS::IMachineMotionPlanningService& service,const std::string& reportPath={}){
    auto q=ActualGantryPoleWindow();std::size_t unresolved=0,certifiedSpins=0,allCertificates=0,continuity=0;
    q.Progress=[&](const auto& stage,auto,auto){if(stage=="SeedingContinuity")++continuity;};
    q.CheckMotion=[&](auto,const auto& a,const auto& b){++allCertificates;const auto spin=std::abs(a[4]-b[4]);
        // This exact owner certificate permits C-spin only in the vertical
        // safe interval. A tilted spin is physically rejected, never softened.
        if(spin>1e-8&&(std::abs(a[5])>1e-8||std::abs(b[5])>1e-8))return MM::Validation{MM::Verdict::Rejected,"C spin outside the known vertical safe interval"};
        if(spin>.2){++unresolved;return MM::Validation{MM::Verdict::Unresolved,"finite owner proof allowance; subdivide this same joint-linear interval"};}
        if(spin>1e-8)++certifiedSpins;return MM::Validation{MM::Verdict::Accepted,"complete analytic empty-environment submove certificate"};};
    const auto result=Run(service,q);std::cout<<"  actual pole 3607..3610 status="<<static_cast<int>(result.State)<<" IK="<<result.IKCalls<<" candidates="<<result.CandidateCount<<" continuity="<<continuity<<" checks="<<allCertificates<<" subdivisions="<<certifiedSpins<<" ms="<<result.ElapsedMs<<" diagnostic="<<result.Diagnostic<<'\n';
    if(result.State!=MM::Status::Solved)throw std::runtime_error("future-seeded actual pole window: "+result.Diagnostic);
    Check(unresolved>0&&certifiedSpins>=8,"pole spin is split and EVERY executed submove certified after the complete parent was unresolved");
    std::set<std::size_t> endpoints;bool future=false;for(std::size_t i=0;i<result.Samples.size();++i){const auto& s=result.Samples[i];future|=s.SeedSource.find("FutureStation")!=std::string::npos;
        if(s.SourceStation==std::numeric_limits<std::size_t>::max())continue;
        if(s.SourceFraction==1){endpoints.insert(s.SourceStation);const auto& goal=q.Stations[s.SourceStation].Goal;for(int k=0;k<3;++k){Near(s.TCP[k],goal.PositionMm[k],.010001,"original real station TCP preserved");Near(s.Direction[k],goal.Direction[k],.000101,"original real station direction preserved");}}
        if(i&&std::abs(s.Positions[4]-result.Samples[i-1].Positions[4])>1e-8){Check(s.SourceStation<2,"C rotation completed before leaving the original vertical stations");Near(s.Positions[5],0,1e-8,"certified C-spin endpoint remains vertical");}
    }
    Check(endpoints.size()==4&&future,"all four original stations and future-seeded inverse provenance retained");Check(result.MaximumPositionErrorMm<=.01&&result.MaximumDirectionErrorRad<=1e-4,"tight TCP/direction validation unchanged");
    if(!reportPath.empty()){
        std::ofstream f(reportPath);f.precision(17);const auto array=[&](const auto& values){f<<'[';bool comma=false;for(const auto& value:values){if(comma)f<<',';comma=true;f<<value;}f<<']';};
        f<<"{\"nodes\":[";for(std::size_t i=0;i<q.Machine.Nodes.size();++i){if(i)f<<',';const auto& n=q.Machine.Nodes[i];f<<"{\"id\":\""<<n.ID<<"\",\"parent\":"<<n.Parent<<",\"type\":\""<<(n.Type==MK::JointType::Fixed?"fixed":n.Type==MK::JointType::Prismatic?"prismatic":"revolute")<<"\",\"origin\":";array(n.Origin);f<<",\"axis\":";array(n.Axis);f<<'}';}
        f<<"],\"tcp\":";array(q.TaskDefinition.TCP);f<<",\"beam\":";array(q.TaskDefinition.Beam);f<<",\"initialJoints\":";array(q.InitialPositions);f<<",\"stations\":[";
        for(std::size_t i=0;i<q.Stations.size();++i){if(i)f<<',';f<<"{\"source\":"<<3607+i<<",\"position\":";array(q.Stations[i].Goal.PositionMm);f<<",\"direction\":";array(q.Stations[i].Goal.Direction);f<<'}';}
        f<<"],\"samples\":[";for(std::size_t i=0;i<result.Samples.size();++i){if(i)f<<',';const auto& s=result.Samples[i];f<<"{\"source\":\""<<(s.SourceStation==std::numeric_limits<std::size_t>::max()?"Initial":std::to_string(3607+s.SourceStation))<<"\",\"fraction\":"<<s.SourceFraction<<",\"timeSeconds\":"<<s.TimeSeconds<<",\"joints\":";array(s.Positions);f<<",\"tcp\":";array(s.TCP);f<<",\"direction\":";array(s.Direction);f<<",\"seed\":\""<<s.SeedSource<<"\"}";}
        f<<"],\"unresolvedParents\":"<<unresolved<<",\"certifiedSpinSubmoves\":"<<certifiedSpins<<",\"ownerMotionChecks\":"<<allCertificates<<",\"ikCalls\":"<<result.IKCalls<<",\"elapsedMs\":"<<result.ElapsedMs<<",\"maximumPositionErrorMm\":"<<result.MaximumPositionErrorMm<<",\"maximumDirectionErrorRad\":"<<result.MaximumDirectionErrorRad<<'}';Check(bool(f),"write independent pole continuity audit fixture");
    }
    q=ActualGantryPoleWindow();q.CheckMotion=[](auto,const auto& a,const auto& b){return MM::Validation{std::abs(a[4]-b[4])>.2?MM::Verdict::Rejected:MM::Verdict::Accepted,"actual C-spin obstacle"};};const auto blocked=Run(service,q);Check(blocked.State!=MM::Status::Solved&&blocked.Samples.empty(),"an actual rejected spin is never bypassed by unresolved subdivision");
    q=ActualGantryPoleWindow();bool stop=false;q.CheckMotion=[&](auto,const auto& a,const auto& b){if(std::abs(a[4]-b[4])>.2)stop=true;return MM::Validation{stop?MM::Verdict::Unresolved:MM::Verdict::Accepted,"cancel during pole certificate"};};const auto cancelled=Run(service,q,[&]{return stop;});Check(cancelled.State==MM::Status::Cancelled&&cancelled.Samples.empty(),"cancel during unresolved pole certification publishes no partial route");
}
void ActualMachine(GS::IMachineMotionPlanningService& service,const std::string& path,const std::string& tool,const std::string& support){
    namespace M=iCAX::CAM::Motion;iCAX::CAM::CMachineDescriptionResource description;std::string error;Check(iCAX::CAM::LoadMachineDescription(std::string(ICAX_TEST_REPO_ROOT)+path,description,error),"actual machine normalized loader");
    MM::Query q;std::vector<M::Node> independent;std::map<std::string,int> ids;std::vector<const iCAX::CAM::SMachineElementData*> pending;for(const auto& e:description.Elements)pending.push_back(&e);int toolIndex=-1,supportIndex=-1;
    while(!pending.empty()){bool progress=false;for(auto it=pending.begin();it!=pending.end();){const auto& e=**it;if(!e.ParentElementID.empty()&&!ids.contains(e.ParentElementID)){++it;continue;}MK::Node n;n.ID=e.ElementID;n.Parent=e.ParentElementID.empty()?-1:ids.at(e.ParentElementID);const auto& p=e.JointToParent.bValid?e.JointToParent.Pose:e.Pose;M::Node rotate;rotate.Type="revolute";rotate.Axis={0,0,1};n.Origin=M::Joint(rotate,p[5]);rotate.Axis={0,1,0};n.Origin=M::Multiply(n.Origin,M::Joint(rotate,p[4]));rotate.Axis={1,0,0};n.Origin=M::Multiply(n.Origin,M::Joint(rotate,p[3]));for(int k=0;k<3;++k)n.Origin[4*k+3]=p[k]*1000;
        const auto& j=e.JointToParent;std::string type="fixed";if(j.bValid&&j.JointType!="fixed"){type=j.JointType;n.Type=type=="prismatic"?MK::JointType::Prismatic:MK::JointType::Revolute;for(int k=0;k<3;++k)n.Axis[k]=j.Axis.XYZ[k].To<double>();n.Axis=M::Unit(n.Axis);n.Lower=j.dLowerLimit*(type=="prismatic"?1000:1);n.Upper=j.dUpperLimit*(type=="prismatic"?1000:1);n.ReferenceValue=std::clamp(0.,n.Lower,n.Upper);}
        if(e.OriginalName==tool){toolIndex=int(q.Machine.Nodes.size());q.TaskDefinition.ToolFrameID=n.ID;if(e.Tool.bValid)for(int k=0;k<3;++k){q.TaskDefinition.TCP[k]=e.Tool.TcpLocalPosition[k].To<double>();q.TaskDefinition.Beam[k]=e.Tool.BeamLocalDirection[k].To<double>();}}
        if(e.OriginalName==support){supportIndex=int(q.Machine.Nodes.size());q.TaskDefinition.ReferenceFrameID=n.ID;}M::Node o;o.ID=n.ID;o.Parent=n.Parent;o.Origin=n.Origin;o.Axis=n.Axis;o.Type=type;independent.push_back(o);ids[n.ID]=int(q.Machine.Nodes.size());q.InitialPositions.push_back(n.Type==MK::JointType::Fixed?0:n.ReferenceValue);q.MaximumVelocities.push_back(type=="fixed"?0:type=="prismatic"?100:1);q.MaximumAxisSteps.push_back(type=="fixed"?0:n.Upper-n.Lower);q.Machine.Nodes.push_back(n);it=pending.erase(it);progress=true;}Check(progress,"actual normalized tree ordering");}
    Check(toolIndex>=0&&supportIndex>=0,"actual tool/reference links");if(tool=="A 激光切割头"){q.TaskDefinition.TCP={0,0,-220};q.TaskDefinition.Beam={0,0,-1};}
    auto initial=q.InitialPositions;std::size_t number=0;for(std::size_t i=0;i<initial.size();++i)if(q.Machine.Nodes[i].Type!=MK::JointType::Fixed){const auto& n=q.Machine.Nodes[i];initial[i]=std::clamp(n.Type==MK::JointType::Prismatic?20.:std::array<double,6>{.2,-.4,.6,.3,-.5,.7}[number++%6],n.Lower,n.Upper);}q.InitialPositions=initial;
    const auto oracle=[independent,task=q.TaskDefinition,toolIndex,supportIndex](const std::vector<double>& positions){const auto world=M::Forward(independent,positions);const auto relative=M::Multiply(M::Inverse(world[supportIndex]),world[toolIndex]);MM::Reference r;r.Goal.PositionMm=M::Transform(relative,task.TCP);r.Goal.Direction=M::Unit(M::Transform(relative,task.Beam,true));return r;};
    std::vector<std::vector<double>> source;for(int sample=0;sample<3;++sample){auto positions=initial;for(std::size_t i=0;i<positions.size();++i)if(q.Machine.Nodes[i].Type!=MK::JointType::Fixed)positions[i]+=sample*(q.Machine.Nodes[i].Type==MK::JointType::Prismatic?.2:.0002);source.push_back(positions);MM::Station station;station.Goal=oracle(positions).Goal;station.ID="actual-"+std::to_string(sample);station.Role="normalized-resource-test";q.Stations.push_back(station);}
    q.ReferenceAt=[source,oracle](std::size_t edge,double u,const auto&,const auto&){auto positions=source.at(edge-1);for(std::size_t i=0;i<positions.size();++i)positions[i]+=u*(source.at(edge)[i]-positions[i]);return oracle(positions);};q.CheckState=[](auto,const auto&){return MM::Validation{MM::Verdict::Accepted,"known empty motion-service test environment; asset mesh collision is not asserted"};};q.CheckMotion=[](auto,const auto&,const auto&){return MM::Validation{MM::Verdict::Accepted,"complete linear edge in known empty test environment"};};q.IKSeedsPerTarget=1;q.IKCandidatesPerTarget=1;q.IKTimeMsPerTarget=1000;q.MaximumSeconds=20;q.DirectionToleranceRad=.001;q.PositionToleranceMm=.05;
    const auto result=Run(service,q);if(result.State!=MM::Status::Solved)throw std::runtime_error("actual machine motion service: "+result.Diagnostic);Check(!result.Samples.empty()&&result.IKCalls==3&&result.IKSeeds<=3,"actual machine bounded inverse calls with analytic or warm numerical candidates");for(const auto& s:result.Samples){const auto expected=oracle(s.Positions);Near(s.TCP[0],expected.Goal.PositionMm[0],1e-7,"actual machine motion independent FK");}std::cout<<"  actual model "<<description.SourceFormat<<" stations=3 samples="<<result.Samples.size()<<" seeds="<<result.IKSeeds<<" maxTCPmm="<<result.MaximumPositionErrorMm<<" ms="<<result.ElapsedMs<<'\n';
}
}
void PolynomialCallbackAndEventRegressions(GS::IMachineMotionPlanningService& service){
    for(const auto verdict:{MM::Verdict::Rejected,MM::Verdict::Unresolved}){
        auto query=Linear();std::size_t calls=0;
        query.CheckTrajectory=[&](const MM::AxisSegment&){++calls;return MM::Validation{verdict,"complete polynomial checker refused the curve"};};
        const auto result=Run(service,query);
        Check(calls>0&&result.State==(verdict==MM::Verdict::Rejected?MM::Status::Rejected:MM::Status::Unresolved)&&result.Samples.empty()&&result.Segments.empty(),"a refused complete polynomial certificate cannot publish a partial trajectory");
    }
    auto query=Linear();query.Stations.insert(query.Stations.begin()+1,query.Stations.front());
    query.ReferenceAt=[stations=query.Stations](std::size_t edge,double u,const auto&,const auto&){MM::Reference reference;reference.Goal=stations[edge-1].Goal;for(int k=0;k<3;++k)reference.Goal.PositionMm[k]+=u*(stations[edge].Goal.PositionMm[k]-reference.Goal.PositionMm[k]);return reference;};
    const auto result=Run(service,query);Check(result.State==MM::Status::Solved,"coincident event route solves");bool found=false;
    for(std::size_t k=1;k<result.Samples.size();++k)if(result.Samples[k].SourceStation==1){const auto& event=result.Samples[k];const auto& previous=result.Samples[k-1];Check(event.TimeSeconds==previous.TimeSeconds&&event.Velocities==previous.Velocities&&event.Accelerations==previous.Accelerations,"zero-time process event preserves the actual continuous boundary state");Check(std::abs(event.Velocities[1])>1e-3,"coincident event does not insert an unnecessary stop");found=true;}
    Check(found,"coincident source event is retained");
}
int main(int argc,char** argv){try{
    Check(LoadLibraryA(ICAX_KINEMATICS_DLL)!=nullptr,"load actual Pinocchio/Ceres plugin");Check(LoadLibraryA("DescartesPlanningServices.dll")!=nullptr,"load actual Descartes adapter");Check(LoadLibraryA("MachineMotionPlanningServices.dll")!=nullptr,"load actual motion planning plugin");
    iCAX::Services::CServiceProvider provider;iCAX::Services::CServiceRegistrationCatalog::ReplayAll(provider);const auto service=provider.Resolve<GS::IMachineMotionPlanningService>();const auto graph=provider.Resolve<GS::IPathOrientationPlanningService>();
    if(argc>=2&&std::string(argv[1])=="--optional-limits"){OptionalDynamics(*service);std::cout<<"PASS optional limits: full unbounded, mixed finite/unbounded, finite polynomial derivatives, C2 joins and collisions\n";return 0;}
    if(argc>=2&&std::string(argv[1])=="--copp-offset"){auto q=OffsetRotary(1.398114);q.MaximumSeconds=120;q.MaximumVerificationSamples=2000000;std::string stage;q.Progress=[&](const auto& value,auto at,auto total){if(value!=stage){stage=value;std::cerr<<"stage="<<stage<<" at="<<at<<" total="<<total<<'\n';}};const auto result=Run(*service,q);std::cerr<<"result="<<int(result.State)<<" ms="<<result.ElapsedMs<<" probes="<<result.VerificationSamples<<" motion="<<result.MotionChecks<<" segments="<<result.Segments.size()<<" "<<result.Diagnostic<<'\n';return result.State==MM::Status::Solved?0:1;}
    if(argc>=2&&std::string(argv[1])=="--actual-pole-window"){FutureSeededPoleContinuity(*service,argc>=3?argv[2]:"");IndependentLinearCertificateDepth(*service);std::cout<<"PASS actual gantry pole 3607..3610 continuity and full submove certificates\n";return 0;}
    auto q=Linear();std::set<std::size_t> edges;q.CheckMotion=[&](auto edge,const auto&,const auto&){edges.insert(edge);return MM::Validation{MM::Verdict::Accepted,"complete edge in empty environment"};};const auto solved=Run(*service,q);
    Check(solved.State==MM::Status::Solved&&!solved.Samples.empty(),"real Pinocchio/Ceres + Descartes linear plan");Check(edges==std::set<std::size_t>{0,1,2},"initial approach plus every whole axis edge certified");Check(!solved.MachineReady&&solved.InterpolationCoverage=="SampledFK;CertifiedPolynomialDynamics"&&solved.TimingQualification=="VelocityAccelerationJerk","qualification remains explicit");Check(solved.DurationSeconds>3,"acceleration and jerk produce physical rest-to-rest timing");Check(!solved.Segments.empty()&&solved.MaximumVelocityRatio<=1&&solved.MaximumAccelerationRatio<=1&&solved.MaximumJerkRatio<=1,"whole-polynomial dynamic bounds");Near(solved.Samples.front().Positions[1],90,1e-8,"initial actual axis state preserved");Near(solved.Samples.back().Positions[1],120,1e-6,"last actual axis target");Check(solved.IKSeeds==3,"one warm seed per station, no hidden sixteen-seed search");
    double time=-1;for(const auto& s:solved.Samples){Check(s.TimeSeconds>=time,"monotonic time");time=s.TimeSeconds;Near(s.TCP[0],s.Positions[1],1e-7,"actual FK rendered sample");Check(s.PositionErrorMm<=q.PositionToleranceMm,"adaptive TCP sample tolerance");}Check(solved.Samples.back().StationID=="cut-2"&&!solved.Samples.back().SeedSource.empty(),"source ID + numerical seed provenance");
    q=Linear();q.CheckMotion=[](auto edge,const auto&,const auto&){return MM::Validation{edge?MM::Verdict::Accepted:MM::Verdict::Rejected,"unsafe initial approach"};};const auto approach=Run(*service,q);Check(approach.State==MM::Status::Rejected&&approach.Samples.empty()&&approach.FailedEdge==0,"unsafe initial connection rejected, no partial playback");
    q=Linear();q.CheckMotion=[](auto edge,const auto&,const auto&){return MM::Validation{edge==1?MM::Verdict::Rejected:MM::Verdict::Accepted,"interior complete-edge collision"};};const auto collision=Run(*service,q);Check(collision.State==MM::Status::Rejected&&collision.Samples.empty()&&collision.FailedEdge==1,"endpoint-safe interior collision rejected");
    q=Linear();q.CheckMotion=[](auto,const auto&,const auto&){return MM::Validation{MM::Verdict::Unresolved,"owner collision budget exhausted"};};const auto unresolved=Run(*service,q);Check(unresolved.State==MM::Status::Unresolved&&unresolved.Samples.empty(),"unresolved edge never becomes playable");
    q=Linear();const auto normal=q.ReferenceAt;q.ReferenceAt=[normal](auto edge,double u,const auto& a,const auto& b){auto r=normal(edge,u,a,b);r.Goal.PositionMm[1]+=4*u*(1-u);return r;};const auto curved=Run(*service,q);Check(curved.State==MM::Status::Rejected&&curved.Samples.empty()&&curved.MaximumPositionErrorMm>.5,"actual joint interpolation fails an exact curved TCP reference");
    q=Linear();q.MaximumVerificationSamples=1;const auto bounded=Run(*service,q);Check(bounded.State==MM::Status::BudgetExhausted&&bounded.Samples.empty(),"adaptive verification budget explicit");q=Linear();const auto cancelled=Run(*service,q,[]{return true;});Check(cancelled.State==MM::Status::Cancelled&&cancelled.Samples.empty(),"cancel no partial plan");q.MaximumSeconds=1e-9;const auto timed=Run(*service,q);Check(timed.State==MM::Status::BudgetExhausted&&timed.Samples.empty(),"overall deadline explicit");
    // The old local-edit behavior remains locked by default. Explicit finite
    // machine graph mode may choose legal branches at BOTH endpoint stations.
    GS::PathOrientationQuery branches;branches.Axes={{-2,2,4,1}};for(int i=0;i<2;++i){GS::OrientationWaypoint w;w.Original={{0,0,1},{-1}};w.Candidates={{{0,0,1},{1}}};w.MaximumDeviationRadians=3.141592653589793;branches.Waypoints.push_back(w);}branches.CheckState=[](auto,const auto& p){return p.Axes[0]>0;};branches.CheckMotion=[](auto,const auto&,const auto&){return true;};Check(graph->Plan(branches).Status=="NoSolution","default endpoint branch remains locked");branches.AllWaypointsEditable=true;const auto selected=graph->Plan(branches);Check(selected.Status=="Solved"&&selected.Orientations.front().Axes[0]==1&&selected.Orientations.back().Axes[0]==1,"explicit endpoint alternatives use real candidate graph");
    ActualMachine(*service,"/src/apps/five-axis-cam/resources/machines/industrial-ac/LC-3015.urdf","A 激光切割头","工作台");ActualMachine(*service,"/src/apps/five-axis-cam/resources/machines/industrial-arm/IR-1600.sdf","J6 激光加工头","工件装夹台");
    SparseKeyposes(*service);
    AdaptiveInverseSubdivision(*service);
    FutureSeededPoleContinuity(*service);
    IndependentLinearCertificateDepth(*service);
    PolynomialCallbackAndEventRegressions(*service);
    std::cout<<"PASS real motion services: warm IK, Descartes selection, initial/full edges, adaptive actual FK errors, velocity times, provenance, rejection/unresolved/cancel/budget no partials, default locked vs explicit all-endpoint graph\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
