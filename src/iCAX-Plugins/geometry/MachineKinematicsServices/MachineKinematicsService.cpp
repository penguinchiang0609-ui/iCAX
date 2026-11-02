#include "KinematicsInternal.h"
#include <Services/ServicesHelper.h>
namespace {
using namespace iCAX::GeometryServices;
namespace MK=MachineKinematics;
class Service final:public IMachineKinematicsService {
    AUTO_REGIST_SERVICE(IMachineKinematicsService,Service)
public:
    void OnLoad() override{}
    void OnUnload() override{}
    MK::ModelRef BuildModel(const MK::Snapshot& s)const override{return MK::Detail::Build(s);}
    MK::ForwardResult Forward(const MK::ModelRef& m,const std::vector<double>& q,const MK::Task& t,const std::vector<std::string>& frames)const override{return MK::Detail::Evaluate(MK::Detail::Require(m),q,t,false,frames).Pose;}
    MK::JacobianResult Jacobian(const MK::ModelRef& model,const std::vector<double>& q,const MK::Task& t)const override{
        const auto& m=MK::Detail::Require(model);const auto e=MK::Detail::Evaluate(m,q,t,true);std::vector<std::size_t> axes;std::vector<double> scales;
        for(std::size_t i=0;i<m.Source.Nodes.size();++i)if(m.QIndices[i]>=0){axes.push_back(i);scales.push_back(std::max(1e-12,m.Source.Nodes[i].Upper-m.Source.Nodes[i].Lower));}
        return MK::Detail::Condition(m,e.Jacobian,axes,scales,100,1,1e-8);
    }
    MK::Result InverseCandidates(const MK::ModelRef& m,const MK::Request& r,const std::function<bool()>& cancelled)const override{return MK::Detail::Solve(m,r,cancelled);}
};
}
// Also permits an isolated native smoke test without constructing a product,
// database or resource repository. Production discovery uses IService above.
extern "C" __declspec(dllexport) iCAX::GeometryServices::IMachineKinematicsService* CreateMachineKinematicsService(){return new Service;}
extern "C" __declspec(dllexport) void DestroyMachineKinematicsService(iCAX::GeometryServices::IMachineKinematicsService* p){delete p;}
