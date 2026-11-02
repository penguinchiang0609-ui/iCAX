#pragma once
#include <Services/IService.h>
#include <array>
#include <functional>
#include <limits>
#include <memory>
#include <string>
#include <vector>

namespace iCAX::GeometryServices::MachineKinematics {
using Vector=std::array<double,3>;
using Matrix=std::array<double,16>;
inline Matrix Identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
enum class JointType {Fixed,Prismatic,Revolute,Continuous};
struct Node {
    std::string ID;
    int Parent=-1;
    JointType Type=JointType::Fixed;
    // Row-major rigid transform, column vectors; translations mm. Joint motion
    // follows Origin, about/along Axis expressed in this joint's local frame.
    Matrix Origin=Identity();
    Vector Axis{1,0,0};
    // Physical coordinates: mm for prismatic, radians for angular joints.
    // Continuous joints also require a finite owner-declared turn domain.
    double Lower=0,Upper=0,ReferenceValue=0;
};
struct Snapshot {std::vector<Node> Nodes;std::string SourceDigest;};
class Model {public:virtual ~Model()=default;};
using ModelRef=std::shared_ptr<const Model>;
enum class TaskKind {PositionAndDirection,FullPose};
struct Task {
    std::string ToolFrameID,ReferenceFrameID;
    // TCP and unit beam in ToolFrameID. Empty ReferenceFrameID means world.
    Vector TCP{},Beam{0,0,-1};
    TaskKind Kind=TaskKind::PositionAndDirection;
};
struct Target {
    // All targets are expressed in ReferenceFrameID, NOT world.
    Vector PositionMm{},Direction{0,0,-1};
    // FullPose only: relative Tool-frame orientation (TCP is a translation
    // only; it has exactly the Tool frame's orientation). Translation ignored.
    Matrix Orientation=Identity();
};
struct FramePose {std::string ID;Matrix World=Identity();};
struct ForwardResult {
    std::vector<FramePose> Frames;
    Matrix RelativeTool=Identity();
    Vector PositionMm{},Direction{0,0,-1};
};
struct JacobianResult {
    // Row-major 6 x Nodes.size(), derivatives in physical coordinates.
    // PositionAndDirection: [dPosition;dUnitDirection], latter rank <=2; never
    // imposes roll. FullPose: [dPosition;relative spatial angular velocity].
    std::vector<double> Values;
    std::size_t Columns=0;
    // Singular values of the dimensionless active task Jacobian. Rank uses
    // RelativeRankTolerance; zero singular values from fixed columns excluded.
    std::vector<double> SingularValues;
    std::size_t Rank=0;
    double ReciprocalCondition=0;
};
struct AxisConstraint {
    std::string AxisID;
    double Lower=-std::numeric_limits<double>::infinity();
    double Upper=std::numeric_limits<double>::infinity();
    bool Fixed=false;
    double FixedValue=0;
};
struct Request {
    Task TaskDefinition;
    Target Goal;
    // q always has one slot per Snapshot.Nodes, fixed slots exactly zero.
    std::vector<std::vector<double>> Seeds;
    std::vector<AxisConstraint> Constraints;
    std::size_t MaxSeeds=16,MaxIterations=160,MaxCandidates=8;
    double MaxTimeMs=1000,PositionToleranceMm=0.01,DirectionToleranceRad=1e-5;
    double PositionScaleMm=100,DirectionScale=1,RelativeRankTolerance=1e-8;
    double DeduplicationTolerance=1e-6;
};
struct Candidate {
    std::vector<double> Positions,LimitMargins;
    // OrientationErrorRad is meaningful only for a supported FullPose solve.
    double PositionErrorMm=0,DirectionErrorRad=0,OrientationErrorRad=0;
    double MinNormalizedLimitMargin=0,SeedDistance=0;
    JacobianResult Conditioning;
    std::size_t SeedIndex=0;
    std::string SeedSource,BranchKey;
};
enum class Status {Solved,NoCandidate,Unsupported,Cancelled,BudgetExhausted};
struct Result {
    Status State=Status::NoCandidate;
    std::vector<Candidate> Candidates;
    // Local numerical search is finite. NoCandidate/BudgetExhausted does not
    // establish physical unreachability or enumerate all inverse branches.
    std::string Coverage="LocalNumericalSearch",Provider,ProviderVersion,ModelDigest,Diagnostic;
    std::size_t SeedsAttempted=0,Evaluations=0,Iterations=0;
    double ElapsedMs=0;
    bool SeedBudgetExhausted=false,TimeBudgetExhausted=false;
};
}
namespace iCAX::GeometryServices {
class IMachineKinematicsService:public Services::IService {
public:
    // Immutable normalized tree. Mutable Pinocchio Data is local to each call.
    virtual MachineKinematics::ModelRef BuildModel(const MachineKinematics::Snapshot&) const=0;
    virtual MachineKinematics::ForwardResult Forward(const MachineKinematics::ModelRef&,
        const std::vector<double>&,const MachineKinematics::Task&,
        const std::vector<std::string>& frameIDs={}) const=0;
    virtual MachineKinematics::JacobianResult Jacobian(const MachineKinematics::ModelRef&,
        const std::vector<double>&,const MachineKinematics::Task&) const=0;
    virtual MachineKinematics::Result InverseCandidates(const MachineKinematics::ModelRef&,
        const MachineKinematics::Request&,const std::function<bool()>& cancelled={}) const=0;
};
}
