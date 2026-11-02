#pragma once
#include <GeometryServices/MachineKinematicsService.h>
#include <Eigen/Core>
#include <pinocchio/multibody/model.hpp>
#include <unordered_map>
namespace iCAX::GeometryServices::MachineKinematics::Detail {
struct Prepared final:Model {
    Snapshot Source;
    pinocchio::Model Tree;
    std::vector<pinocchio::FrameIndex> Frames;
    std::vector<int> QIndices;
    std::unordered_map<std::string,std::size_t> IDs;
    std::string Digest;
};
struct Evaluation {
    ForwardResult Pose;
    Eigen::MatrixXd Jacobian;
};
const Prepared& Require(const ModelRef&);
Evaluation Evaluate(const Prepared&,const std::vector<double>&,const Task&,bool derivatives,
    const std::vector<std::string>& frameIDs={});
JacobianResult Condition(const Prepared&,const Eigen::MatrixXd&,const std::vector<std::size_t>&,
    const std::vector<double>& scales,double positionScale,double directionScale,double rankTolerance);
ModelRef Build(const Snapshot&);
Result Solve(const ModelRef&,const Request&,const std::function<bool()>&);
}
