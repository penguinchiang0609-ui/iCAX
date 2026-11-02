#include "KinematicsInternal.h"
#include <pinocchio/algorithm/frames.hpp>
#include <pinocchio/algorithm/jacobian.hpp>
#include <pinocchio/multibody/joint/joint-revolute-unaligned.hpp>
#include <pinocchio/multibody/joint/joint-prismatic-unaligned.hpp>
#include <Eigen/SVD>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <sstream>
#include <stdexcept>

namespace iCAX::GeometryServices::MachineKinematics::Detail {
namespace {
Eigen::Vector3d Vec(const Vector& v){for(auto x:v)if(!std::isfinite(x))throw std::invalid_argument("KINEMATICS_VECTOR_INVALID");return {v[0],v[1],v[2]};}
Vector Array(const Eigen::Vector3d& p){return {p.x(),p.y(),p.z()};}
pinocchio::SE3 Rigid(const Matrix& a){
    for(auto x:a)if(!std::isfinite(x))throw std::invalid_argument("KINEMATICS_TRANSFORM_INVALID");
    Eigen::Matrix3d r;for(int i=0;i<3;++i)for(int j=0;j<3;++j)r(i,j)=a[i*4+j];
    if((r.transpose()*r-Eigen::Matrix3d::Identity()).norm()>1e-8||std::abs(r.determinant()-1)>1e-8||
       std::abs(a[12])+std::abs(a[13])+std::abs(a[14])+std::abs(a[15]-1)>1e-8)throw std::invalid_argument("KINEMATICS_TRANSFORM_INVALID");
    return {r,Eigen::Vector3d(a[3],a[7],a[11])};
}
Matrix Array(const pinocchio::SE3& p){auto a=Identity();for(int i=0;i<3;++i){for(int j=0;j<3;++j)a[i*4+j]=p.rotation()(i,j);a[i*4+3]=p.translation()[i];}return a;}
// Digest identifies the actual normalized model, independently of owner labels.
std::string Digest(const Snapshot& s){std::ostringstream out;out<<std::hexfloat;for(const auto& n:s.Nodes){out<<n.ID<<'\0'<<n.Parent<<int(n.Type);for(auto x:n.Origin)out<<x<<',';for(auto x:n.Axis)out<<x<<',';out<<n.Lower<<','<<n.Upper<<','<<n.ReferenceValue<<';';}std::uint64_t h=1469598103934665603ull;for(unsigned char c:out.str()){h^=c;h*=1099511628211ull;}std::ostringstream hex;hex<<std::hex<<h;return hex.str();}
}
const Prepared& Require(const ModelRef& model){auto p=dynamic_cast<const Prepared*>(model.get());if(!p)throw std::invalid_argument("KINEMATICS_MODEL_PROVIDER_MISMATCH");return *p;}
ModelRef Build(const Snapshot& snapshot){
    if(snapshot.Nodes.empty())throw std::invalid_argument("KINEMATICS_EMPTY_TREE");
    auto p=std::make_shared<Prepared>();p->Source=snapshot;p->Digest=Digest(snapshot);
    // Owner snapshots need only be parent-before-child. Pinocchio additionally
    // requires each joint subtree to occupy contiguous internal joint slots.
    // Keep the public node/q/frame slots unchanged while building that private
    // joint order depth first, including independently moving support branches.
    std::vector<std::vector<std::size_t>> children(snapshot.Nodes.size());std::vector<std::size_t> roots;
    for(std::size_t i=0;i<snapshot.Nodes.size();++i){
        const auto& n=snapshot.Nodes[i];
        if(n.ID.empty()||!p->IDs.emplace(n.ID,i).second||n.Parent<-1||n.Parent>=int(i))throw std::invalid_argument("KINEMATICS_TREE_INVALID");
        if(n.Parent<0)roots.push_back(i);else children[n.Parent].push_back(i);
    }
    std::vector<std::size_t> order,stack;for(auto it=roots.rbegin();it!=roots.rend();++it)stack.push_back(*it);
    while(!stack.empty()){const auto i=stack.back();stack.pop_back();order.push_back(i);for(auto it=children[i].rbegin();it!=children[i].rend();++it)stack.push_back(*it);}
    std::vector<pinocchio::JointIndex> joints(snapshot.Nodes.size());std::vector<pinocchio::SE3> offsets(snapshot.Nodes.size(),pinocchio::SE3::Identity());
    p->Frames.resize(snapshot.Nodes.size());p->QIndices.resize(snapshot.Nodes.size(),-1);
    for(const auto i:order){
        const auto& n=snapshot.Nodes[i];
        const auto parent=n.Parent<0?pinocchio::JointIndex(0):joints[n.Parent];
        const auto parentOffset=n.Parent<0?pinocchio::SE3::Identity():offsets[n.Parent];
        const auto placement=parentOffset*Rigid(n.Origin);
        auto joint=parent;auto offset=placement;int qindex=-1;const auto internalName="icax.node."+std::to_string(i);
        if(n.Type!=JointType::Fixed){
            const auto axis=Vec(n.Axis);if(std::abs(axis.norm()-1)>1e-8)throw std::invalid_argument("KINEMATICS_AXIS_NOT_UNIT");
            if(!std::isfinite(n.Lower)||!std::isfinite(n.Upper)||!std::isfinite(n.ReferenceValue)||n.Lower>n.Upper||n.ReferenceValue<n.Lower||n.ReferenceValue>n.Upper)throw std::invalid_argument("KINEMATICS_LIMIT_INVALID");
            if(n.Type==JointType::Prismatic)joint=p->Tree.addJoint(parent,pinocchio::JointModelPrismaticUnaligned(axis),placement,internalName);
            else if(n.Type==JointType::Revolute||n.Type==JointType::Continuous)joint=p->Tree.addJoint(parent,pinocchio::JointModelRevoluteUnaligned(axis),placement,internalName);
            else throw std::invalid_argument("KINEMATICS_JOINT_UNSUPPORTED");
            offset=pinocchio::SE3::Identity();qindex=p->Tree.joints[joint].idx_q();
            p->Tree.lowerPositionLimit[qindex]=n.Lower;p->Tree.upperPositionLimit[qindex]=n.Upper;
        }
        joints[i]=joint;offsets[i]=offset;p->QIndices[i]=qindex;
        const auto parentFrame=n.Parent<0?pinocchio::FrameIndex(0):p->Frames[n.Parent];
        p->Frames[i]=p->Tree.addFrame(pinocchio::Frame(internalName,joint,parentFrame,offset,pinocchio::OP_FRAME),false);
    }
    return p;
}
Evaluation Evaluate(const Prepared& m,const std::vector<double>& values,const Task& task,bool derivatives,const std::vector<std::string>& frameIDs){
    if(values.size()!=m.Source.Nodes.size())throw std::invalid_argument("KINEMATICS_Q_SIZE");
    const auto tool=m.IDs.find(task.ToolFrameID),ref=m.IDs.find(task.ReferenceFrameID);
    if(tool==m.IDs.end()||(!task.ReferenceFrameID.empty()&&ref==m.IDs.end()))throw std::invalid_argument("KINEMATICS_FRAME_UNKNOWN");
    const auto tcp=Vec(task.TCP),beam=Vec(task.Beam);if(std::abs(beam.norm()-1)>1e-8)throw std::invalid_argument("KINEMATICS_BEAM_NOT_UNIT");
    Eigen::VectorXd q=Eigen::VectorXd::Zero(m.Tree.nq);
    for(std::size_t i=0;i<values.size();++i){if(!std::isfinite(values[i]))throw std::invalid_argument("KINEMATICS_Q_NONFINITE");if(m.QIndices[i]>=0)q[m.QIndices[i]]=values[i];else if(values[i]!=0)throw std::invalid_argument("KINEMATICS_FIXED_Q_NOT_ZERO");}
    pinocchio::Data data(m.Tree);
    if(derivatives)pinocchio::computeJointJacobians(m.Tree,data,q);else pinocchio::forwardKinematics(m.Tree,data,q);
    pinocchio::updateFramePlacements(m.Tree,data);
    const auto& tw=data.oMf[m.Frames[tool->second]];const auto rw=task.ReferenceFrameID.empty()?pinocchio::SE3::Identity():data.oMf[m.Frames[ref->second]];
    const auto relative=rw.inverse()*tw;const Eigen::Vector3d pw=tw.act(tcp),dw=tw.rotation()*beam;
    Evaluation e;e.Pose.RelativeTool=Array(relative);e.Pose.PositionMm=Array(relative.act(tcp));e.Pose.Direction=Array(relative.rotation()*beam);
    for(const auto& id:frameIDs){const auto it=m.IDs.find(id);if(it==m.IDs.end())throw std::invalid_argument("KINEMATICS_FRAME_UNKNOWN");e.Pose.Frames.push_back({id,Array(data.oMf[m.Frames[it->second]])});}
    if(!derivatives)return e;
    Eigen::Matrix<double,6,Eigen::Dynamic> jt(6,m.Tree.nv),jr(6,m.Tree.nv);jt.setZero();jr.setZero();
    pinocchio::getFrameJacobian(m.Tree,data,m.Frames[tool->second],pinocchio::LOCAL_WORLD_ALIGNED,jt);
    if(!task.ReferenceFrameID.empty())pinocchio::getFrameJacobian(m.Tree,data,m.Frames[ref->second],pinocchio::LOCAL_WORLD_ALIGNED,jr);
    e.Jacobian=Eigen::MatrixXd::Zero(6,values.size());
    const Eigen::Matrix3d rt=rw.rotation().transpose();
    const Eigen::Vector3d tcpoffset=tw.rotation()*tcp,relativeOffset=pw-rw.translation();
    for(std::size_t i=0;i<values.size();++i)if(m.QIndices[i]>=0){const int k=m.QIndices[i];
        const Eigen::Vector3d wt=jt.block<3,1>(3,k),wr=jr.block<3,1>(3,k);
        e.Jacobian.block<3,1>(0,i)=rt*(jt.block<3,1>(0,k)+wt.cross(tcpoffset)-jr.block<3,1>(0,k)-wr.cross(relativeOffset));
        if(task.Kind==TaskKind::PositionAndDirection)e.Jacobian.block<3,1>(3,i)=rt*((wt-wr).cross(dw));
        else e.Jacobian.block<3,1>(3,i)=rt*(wt-wr);
    }
    return e;
}
JacobianResult Condition(const Prepared& m,const Eigen::MatrixXd& j,const std::vector<std::size_t>& axes,const std::vector<double>& scales,double ps,double ds,double tolerance){
    JacobianResult r;r.Columns=m.Source.Nodes.size();r.Values.resize(6*r.Columns);for(int row=0;row<6;++row)for(std::size_t col=0;col<r.Columns;++col)r.Values[row*r.Columns+col]=j(row,col);
    if(axes.empty())return r;
    Eigen::MatrixXd normalized(6,axes.size());for(std::size_t c=0;c<axes.size();++c){normalized.col(c)=j.col(axes[c])*scales[c];normalized.col(c).head<3>()/=ps;normalized.col(c).tail<3>()/=ds;}
    const auto singular=Eigen::JacobiSVD<Eigen::MatrixXd>(normalized).singularValues();
    for(int k=0;k<singular.size();++k){r.SingularValues.push_back(singular[k]);if(singular[k]>singular[0]*tolerance)++r.Rank;}
    if(r.Rank)r.ReciprocalCondition=singular[r.Rank-1]/singular[0];return r;
}
}
