#include <GeometryServices/MachineKinematicsService.h>
#include <MachineDescriptionLoader.h>
#include "../../../iCAX-Plugins/cam/Machine/MachineMotion.h"
#include <algorithm>
#include <cmath>
#include <future>
#include <iostream>
#include <map>
#include <stdexcept>
#include <fstream>

extern "C" __declspec(dllimport) iCAX::GeometryServices::IMachineKinematicsService* CreateMachineKinematicsService();
extern "C" __declspec(dllimport) void DestroyMachineKinematicsService(iCAX::GeometryServices::IMachineKinematicsService*);
namespace MK=iCAX::GeometryServices::MachineKinematics;
namespace Motion=iCAX::CAM::Motion;
using Service=iCAX::GeometryServices::IMachineKinematicsService;
namespace {
void Check(bool ok,const std::string& message){if(!ok)throw std::runtime_error(message);}
void Near(double a,double b,double eps,const std::string& message){Check(std::isfinite(a)&&std::isfinite(b)&&std::abs(a-b)<=eps,message+" "+std::to_string(a)+" vs "+std::to_string(b));}
Motion::Matrix Pose(const iCAX::CAM::MachinePose6& p){Motion::Node j;j.Type="revolute";j.Axis={0,0,1};auto m=Motion::Joint(j,p[5]);j.Axis={0,1,0};m=Motion::Multiply(m,Motion::Joint(j,p[4]));j.Axis={1,0,0};m=Motion::Multiply(m,Motion::Joint(j,p[3]));for(int i=0;i<3;++i)m[4*i+3]=p[i]*1000;return m;}
struct Fixture {MK::Snapshot Snapshot;MK::Task Task;std::vector<Motion::Node> Oracle;std::map<std::string,std::size_t> Joints;};
Fixture Load(const std::string& relative,const std::string& toolName,const std::string& supportName){
    iCAX::CAM::CMachineDescriptionResource d;std::string error;Check(iCAX::CAM::LoadMachineDescription(std::string(ICAX_TEST_REPO_ROOT)+relative,d,error),"Existing normalized machine loader: "+error);
    Fixture f;std::map<std::string,int> indices;std::vector<const iCAX::CAM::SMachineElementData*> pending;for(const auto& e:d.Elements)pending.push_back(&e);
    while(!pending.empty()){bool progressed=false;for(auto it=pending.begin();it!=pending.end();){const auto& e=**it;if(!e.ParentElementID.empty()&&!indices.contains(e.ParentElementID)){++it;continue;}
        MK::Node n;n.ID=e.ElementID;n.Parent=e.ParentElementID.empty()?-1:indices.at(e.ParentElementID);n.Origin=Pose(e.JointToParent.bValid?e.JointToParent.Pose:e.Pose);
        const auto& j=e.JointToParent;std::string type="fixed";
        if(j.bValid){type=j.JointType;if(type=="prismatic")n.Type=MK::JointType::Prismatic;else if(type=="revolute")n.Type=MK::JointType::Revolute;else if(type=="continuous")n.Type=MK::JointType::Continuous;
            if(n.Type!=MK::JointType::Fixed){Check(j.Axis.XYZ.size()==3,"normalized movable axis missing");for(int k=0;k<3;++k)n.Axis[k]=j.Axis.XYZ[k].To<double>();n.Axis=Motion::Unit(n.Axis);const auto unit=type=="prismatic"?1000.:1.;n.Lower=j.dLowerLimit*unit;n.Upper=j.dUpperLimit*unit;n.ReferenceValue=std::clamp(0.,n.Lower,n.Upper);f.Joints.emplace(j.Name,f.Snapshot.Nodes.size());}}
        if(e.OriginalName==toolName){f.Task.ToolFrameID=n.ID;if(e.Tool.bValid){for(int k=0;k<3;++k){f.Task.TCP[k]=e.Tool.TcpLocalPosition[k].To<double>();f.Task.Beam[k]=e.Tool.BeamLocalDirection[k].To<double>();}}}
        if(e.OriginalName==supportName)f.Task.ReferenceFrameID=n.ID;
        Motion::Node o;o.ID=n.ID;o.Parent=n.Parent;o.Type=type;o.Origin=n.Origin;o.Axis=n.Axis;o.Lower=n.Lower;o.Upper=n.Upper;
        indices[n.ID]=int(f.Snapshot.Nodes.size());f.Snapshot.Nodes.push_back(n);f.Oracle.push_back(o);it=pending.erase(it);progressed=true;}
        Check(progressed,"normalized loader tree not ordered/connected");}
    Check(!f.Task.ToolFrameID.empty()&&!f.Task.ReferenceFrameID.empty(),"actual tool/support frame absent");return f;
}
std::vector<double> Reference(const Fixture& f){std::vector<double> q;for(const auto& n:f.Snapshot.Nodes)q.push_back(n.Type==MK::JointType::Fixed?0:n.ReferenceValue);return q;}
void VerifyFKAndJacobian(Service& service,const Fixture& f,const std::vector<double>& q){
    const auto m=service.BuildModel(f.Snapshot);std::vector<std::string> ids;for(const auto& n:f.Snapshot.Nodes)ids.push_back(n.ID);
    const auto fk=service.Forward(m,q,f.Task,ids);const auto jac=service.Jacobian(m,q,f.Task);const auto expected=Motion::Forward(f.Oracle,q);
    Check(fk.Frames.size()==expected.size(),"frame count");for(std::size_t i=0;i<expected.size();++i)for(int k=0;k<16;++k)Near(fk.Frames[i].World[k],expected[i][k],1e-8,"independent normalized tree FK");
    const auto tool=std::find(ids.begin(),ids.end(),f.Task.ToolFrameID)-ids.begin(),ref=std::find(ids.begin(),ids.end(),f.Task.ReferenceFrameID)-ids.begin();
    const auto relative=Motion::Multiply(Motion::Inverse(expected[ref]),expected[tool]);const auto pos=Motion::Transform(relative,f.Task.TCP),dir=Motion::Transform(relative,f.Task.Beam,true);
    for(int k=0;k<3;++k){Near(fk.PositionMm[k],pos[k],1e-8,"relative TCP mm");Near(fk.Direction[k],dir[k],1e-10,"relative signed beam");}
    for(std::size_t col=0;col<q.size();++col){const auto h=f.Snapshot.Nodes[col].Type==MK::JointType::Prismatic?1e-3:1e-6;if(f.Snapshot.Nodes[col].Type==MK::JointType::Fixed)continue;
        auto qa=q,qb=q;qa[col]-=h;qb[col]+=h;const auto a=service.Forward(m,qa,f.Task),b=service.Forward(m,qb,f.Task);
        for(int row=0;row<3;++row){Near(jac.Values[row*q.size()+col],(b.PositionMm[row]-a.PositionMm[row])/(2*h),2e-5,"Pinocchio analytical TCP Jacobian vs central difference");Near(jac.Values[(row+3)*q.size()+col],(b.Direction[row]-a.Direction[row])/(2*h),2e-7,"Pinocchio analytical direction Jacobian vs central difference");}}
}
MK::Result SolveTarget(Service& service,const Fixture& f,const std::vector<double>& desired,const std::vector<double>& seed){const auto model=service.BuildModel(f.Snapshot);const auto pose=service.Forward(model,desired,f.Task);MK::Request r;r.TaskDefinition=f.Task;r.Goal.PositionMm=pose.PositionMm;r.Goal.Direction=pose.Direction;r.Seeds={seed};r.MaxSeeds=12;r.MaxCandidates=4;r.MaxTimeMs=20000;auto result=service.InverseCandidates(model,r);Check(result.State==MK::Status::Solved&&!result.Candidates.empty(),"real Ceres inverse failed: "+result.Diagnostic);for(const auto& c:result.Candidates){Check(c.PositionErrorMm<=r.PositionToleranceMm&&c.DirectionErrorRad<=r.DirectionToleranceRad,"candidate acceptance residual");for(std::size_t i=0;i<c.Positions.size();++i)Check(c.Positions[i]>=f.Snapshot.Nodes[i].Lower-1e-9&&c.Positions[i]<=f.Snapshot.Nodes[i].Upper+1e-9,"Ceres hard physical bounds");}std::cout<<"  "<<f.Snapshot.SourceDigest<<" candidates="<<result.Candidates.size()<<" attempts="<<result.SeedsAttempted<<" FKmm="<<result.Candidates.front().PositionErrorMm<<" angle="<<result.Candidates.front().DirectionErrorRad<<" rank="<<result.Candidates.front().Conditioning.Rank<<" ms="<<result.ElapsedMs<<'\n';return result;}
}
int main(int argc,char** argv){try{
    std::unique_ptr<Service,decltype(&DestroyMachineKinematicsService)> service(CreateMachineKinematicsService(),DestroyMachineKinematicsService);
    auto lc=Load("/src/apps/five-axis-cam/resources/machines/industrial-ac/LC-3015.urdf","A 激光切割头","工作台");lc.Task.TCP={0,0,-220};lc.Task.Beam={0,0,-1};lc.Snapshot.SourceDigest="LC-3015 existing URDF normalized tree";
    auto arm=Load("/src/apps/five-axis-cam/resources/machines/industrial-arm/IR-1600.sdf","J6 激光加工头","工件装夹台");arm.Snapshot.SourceDigest="IR-1600 existing SDF normalized tree";
    auto q=Reference(lc);q[lc.Joints.at("X")]=350;q[lc.Joints.at("Y")]=-160;q[lc.Joints.at("Z")]=-70;q[lc.Joints.at("C")]=.65;q[lc.Joints.at("A")]=.48;VerifyFKAndJacobian(*service,lc,q);const auto lcSolved=SolveTarget(*service,lc,q,Reference(lc));
    // Independent dimensional oracle from the actual LC-3015 documented two
    // 220 mm offsets, 650 mm Z origin and X -430/+430 compensation.
    const auto lcPose=service->Forward(service->BuildModel(lc.Snapshot),q,lc.Task);
    Near(lcPose.PositionMm[0],350-220*std::sin(.65)*std::sin(.48),1e-8,"LC physical X/TCP offsets");
    Near(lcPose.PositionMm[1],-160+220*std::cos(.65)*std::sin(.48),1e-8,"LC physical Y/TCP offsets");
    Near(lcPose.PositionMm[2],430-70-220*std::cos(.48),1e-8,"LC physical Z/TCP offsets");
    auto aq=Reference(arm);std::size_t index=0;for(const auto& [id,i]:arm.Joints)aq[i]=std::clamp(std::array<double,6>{.3,-.4,.7,.2,-.6,.8}[index++%6],arm.Snapshot.Nodes[i].Lower,arm.Snapshot.Nodes[i].Upper);VerifyFKAndJacobian(*service,arm,aq);const auto armSolved=SolveTarget(*service,arm,aq,Reference(arm));
    const auto armModel=service->BuildModel(arm.Snapshot);const auto armPose=service->Forward(armModel,aq,arm.Task);
    MK::Request roll;roll.TaskDefinition=arm.Task;roll.Goal.PositionMm=armPose.PositionMm;roll.Goal.Direction=armPose.Direction;roll.Seeds={aq,aq};roll.Seeds[0][arm.Joints.at("J6")]=-1.2;roll.Seeds[1][arm.Joints.at("J6")]=1.4;roll.MaxSeeds=2;roll.MaxCandidates=2;roll.MaxTimeMs=20000;
    const auto freeRoll=service->InverseCandidates(armModel,roll);Check(freeRoll.Candidates.size()==2,"5D task must preserve multiple physical roll solutions");
    Near(freeRoll.Candidates[0].Positions[arm.Joints.at("J6")],-1.2,1e-9,"IR roll is not constrained by IJK");Near(freeRoll.Candidates[1].Positions[arm.Joints.at("J6")],1.4,1e-9,"IR second free-roll seed");Check(freeRoll.Candidates[0].Conditioning.Rank==5,"IR generic 5D task Jacobian rank");
    // An independently moving support branch must contribute with the opposite
    // relative-frame derivative; this catches one-chain/world-frame shortcuts.
    auto branched=lc;const auto support=std::find_if(branched.Snapshot.Nodes.begin(),branched.Snapshot.Nodes.end(),[&](const auto& n){return n.ID==branched.Task.ReferenceFrameID;})-branched.Snapshot.Nodes.begin();branched.Snapshot.Nodes[support].Type=MK::JointType::Prismatic;branched.Snapshot.Nodes[support].Axis={0,1,0};branched.Snapshot.Nodes[support].Lower=-500;branched.Snapshot.Nodes[support].Upper=500;branched.Oracle[support].Type="prismatic";branched.Oracle[support].Axis={0,1,0};q[support]=125;VerifyFKAndJacobian(*service,branched,q);
    branched.Snapshot.Nodes[support].Type=MK::JointType::Revolute;branched.Snapshot.Nodes[support].Lower=-3;branched.Snapshot.Nodes[support].Upper=3;branched.Oracle[support].Type="revolute";q[support]=.37;VerifyFKAndJacobian(*service,branched,q);
    const auto model=service->BuildModel(lc.Snapshot);MK::Request request;request.TaskDefinition=lc.Task;const auto target=service->Forward(model,lcSolved.Candidates.front().Positions,lc.Task);request.Goal.PositionMm=target.PositionMm;request.Goal.Direction=target.Direction;
    const auto cancelled=service->InverseCandidates(model,request,[]{return true;});Check(cancelled.State==MK::Status::Cancelled&&cancelled.Candidates.empty(),"pre-cancel no fabricated candidates");
    request.MaxTimeMs=1e-6;Check(service->InverseCandidates(model,request).State==MK::Status::BudgetExhausted,"explicit finite deadline");request.MaxTimeMs=20000;
    request.TaskDefinition.Kind=MK::TaskKind::FullPose;Check(service->InverseCandidates(model,request).State==MK::Status::Unsupported,"FullPose must not silently alter 5D task");request.TaskDefinition.Kind=MK::TaskKind::PositionAndDirection;
    request.Seeds={lcSolved.Candidates.front().Positions};const auto locked=lc.Joints.at("X");request.Constraints={{lc.Snapshot.Nodes[locked].ID,-1500,1500,true,request.Seeds[0][locked]}};const auto fixed=service->InverseCandidates(model,request);Check(!fixed.Candidates.empty(),"fixed axis valid target");for(const auto& c:fixed.Candidates)Near(c.Positions[locked],request.Seeds[0][locked],0,"locked axes eliminated");
    request.Constraints.clear();request.Goal.PositionMm[0]=50000;request.MaxSeeds=2;request.MaxIterations=30;const auto notFound=service->InverseCandidates(model,request);Check(notFound.State==MK::Status::NoCandidate&&notFound.Candidates.empty()&&notFound.Coverage=="AnalyticSerialThreePrismaticTwoRotary","constrained analytic failure must not fabricate reachability");
    int stopCalls=0;request.MaxSeeds=100;const auto interrupted=service->InverseCandidates(model,request,[&]{return ++stopCalls>1;});Check(interrupted.State==MK::Status::Cancelled,"in-search analytic cancellation");
    // The outer angle is physically free at a beam pole: preserve its caller
    // value rather than numerically wandering into a different rotary branch.
    auto pole=Reference(lc);pole[lc.Joints.at("C")]=1.13;pole[lc.Joints.at("X")]=-600;pole[lc.Joints.at("Y")]=-77.8;
    const auto polePose=service->Forward(model,pole,lc.Task);MK::Request poleRequest;poleRequest.TaskDefinition=lc.Task;poleRequest.Goal.PositionMm=polePose.PositionMm;poleRequest.Goal.Direction=polePose.Direction;poleRequest.Seeds={pole};poleRequest.MaxSeeds=1;poleRequest.MaxCandidates=2;
    const auto poleResult=service->InverseCandidates(model,poleRequest);Check(poleResult.State==MK::Status::Solved&&!poleResult.Candidates.empty()&&poleResult.SeedsAttempted==0,"pole solved analytically without numerical seeds");Near(poleResult.Candidates.front().Positions[lc.Joints.at("C")],1.13,1e-12,"outer rotary coordinate retained at pole");
    // General topology recognition cannot depend on a machine label, axis
    // name, intersecting rotary origins or orthogonal Cartesian directions.
    auto general=lc;std::map<std::string,std::string> renamed;
    for(std::size_t i=0;i<general.Snapshot.Nodes.size();++i){auto& n=general.Snapshot.Nodes[i];renamed[n.ID]="neutral-"+std::to_string(i);n.ID=renamed[n.ID];}
    general.Task.ToolFrameID=renamed.at(lc.Task.ToolFrameID);general.Task.ReferenceFrameID=renamed.at(lc.Task.ReferenceFrameID);
    general.Snapshot.Nodes[lc.Joints.at("Y")].Axis=Motion::Unit({.2,1.,.1});
    general.Snapshot.Nodes[lc.Joints.at("A")].Origin[3]=37;general.Snapshot.Nodes[lc.Joints.at("A")].Origin[7]=-19;
    general.Task.TCP={17.,-9.,-200.};general.Task.Beam=Motion::Unit({.1,.2,-1.});
    auto generalQ=Reference(general);generalQ[lc.Joints.at("X")]=150;generalQ[lc.Joints.at("Y")]=-70;generalQ[lc.Joints.at("Z")]=35;generalQ[lc.Joints.at("C")]=.65;generalQ[lc.Joints.at("A")]=.48;
    const auto generalSolved=SolveTarget(*service,general,generalQ,Reference(general));Check(generalSolved.Coverage=="AnalyticSerialThreePrismaticTwoRotary"&&generalSolved.SeedsAttempted==0,"neutral 3P2R topology-derived solution");
    MK::Snapshot antipodal;MK::Node root;root.ID="universe";antipodal.Nodes.push_back(root);MK::Node axis;axis.ID="rotation";axis.Parent=0;axis.Type=MK::JointType::Revolute;axis.Axis={0,1,0};axis.Lower=-3.141592653589793;axis.Upper=3.141592653589793;antipodal.Nodes.push_back(axis);
    MK::Request opposite;opposite.TaskDefinition.ToolFrameID="rotation";opposite.TaskDefinition.ReferenceFrameID="universe";opposite.Goal.Direction={0,0,1};opposite.MaxCandidates=1;opposite.MaxSeeds=1;opposite.MaxTimeMs=20000;
    const auto oppositeModel=service->BuildModel(antipodal);Check(service->InverseCandidates(oppositeModel,opposite).Candidates.empty(),"antipodal stationary seed must fail real angle acceptance");opposite.MaxSeeds=8;const auto oppositeFound=service->InverseCandidates(oppositeModel,opposite);Check(oppositeFound.State==MK::Status::Solved&&oppositeFound.Candidates.front().DirectionErrorRad<=opposite.DirectionToleranceRad,"multi-seed resolves signed antipodal direction");
    bool rejected=false;try{auto invalid=lc.Snapshot;invalid.Nodes.back().Parent=int(invalid.Nodes.size());service->BuildModel(invalid);}catch(const std::invalid_argument&){rejected=true;}Check(rejected,"cyclic/nonordered tree rejected");
    rejected=false;try{auto invalid=lc.Snapshot;invalid.Nodes[lc.Joints.at("X")].Axis={2,0,0};service->BuildModel(invalid);}catch(const std::invalid_argument&){rejected=true;}Check(rejected,"nonunit physical axis rejected");
    // Shared immutable model queried concurrently with per-call Pinocchio Data.
    std::vector<std::future<MK::ForwardResult>> jobs;for(int k=0;k<8;++k)jobs.push_back(std::async(std::launch::async,[&]{return service->Forward(model,lcSolved.Candidates.front().Positions,lc.Task);}));for(auto& job:jobs)Near(job.get().PositionMm[0],target.PositionMm[0],1e-10,"concurrent immutable FK");
    if(argc==3&&std::string(argv[1])=="--targets"){
        std::ifstream input(argv[2],std::ios::binary);Check(bool(input),"target file open");
        auto previous=Reference(lc);double maxPosition=0,maxDirection=0;std::size_t count=0;
        for(;;){MK::Request r;if(!(input>>r.Goal.PositionMm[0]))break;
            Check(bool(input>>r.Goal.PositionMm[1]>>r.Goal.PositionMm[2]>>r.Goal.Direction[0]>>r.Goal.Direction[1]>>r.Goal.Direction[2]),"six finite numbers per target");
            r.TaskDefinition=lc.Task;r.Seeds={previous};r.MaxSeeds=2;r.MaxCandidates=2;r.MaxTimeMs=100;
            const auto result=service->InverseCandidates(model,r);Check(result.State==MK::Status::Solved&&!result.Candidates.empty(),"actual target station "+std::to_string(count)+": "+result.Diagnostic);
            Check(result.Coverage=="AnalyticSerialThreePrismaticTwoRotary","actual target uses topology-derived inverse");
            previous=result.Candidates.front().Positions;const auto actual=service->Forward(model,previous,lc.Task);
            for(int k=0;k<3;++k){Near(actual.PositionMm[k],r.Goal.PositionMm[k],1e-7,"actual target independently evaluated TCP");Near(actual.Direction[k],r.Goal.Direction[k],1e-8,"actual target independently evaluated direction");}
            maxPosition=std::max(maxPosition,result.Candidates.front().PositionErrorMm);maxDirection=std::max(maxDirection,result.Candidates.front().DirectionErrorRad);++count;
        }
        std::cout<<"PASS actual target stations="<<count<<" maximumFKmm="<<maxPosition<<" maximumDirectionRad="<<maxDirection<<"; continuous edge collisions are verified by the motion owner separately\n";
    }
    std::cout<<"PASS Pinocchio/Ceres: two existing normalized machines, independent dimensional FK, analytic Jacobian, relative dual branch, 5D bounded multi-seed IK, free roll, signed antipodal, fixed constraints, hard-limit failure, cancellation, deadline, task separation, concurrent contexts\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
