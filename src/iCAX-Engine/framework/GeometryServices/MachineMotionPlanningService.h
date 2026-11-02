#pragma once
#include <GeometryServices/MachineKinematicsService.h>
#include <GeometryServices/AxisTrajectory.h>
#include <functional>
#include <limits>
#include <string>
#include <vector>
namespace iCAX::GeometryServices::MachineMotion {
namespace MK=MachineKinematics;
enum class Verdict {Accepted,Rejected,Unresolved};
struct Validation {Verdict State=Verdict::Unresolved;std::string Diagnostic;};
struct Station {
    MK::Target Goal;
    // Owner-declared allowable signed directions at this SAME TCP position.
    // Empty means only Goal.Direction. No cut/rapid policy is inferred here.
    std::vector<MK::Vector> AlternativeDirections;
    std::string ID,Role;
    double FeedMmPerSecond=100;
    bool StopAfter=false;
};
struct Reference {
    MK::Target Goal;
    // An owner may leave a rapid's interior direction free while retaining its
    // exact TCP reference. End-station targets still receive FK acceptance.
    bool CheckDirection=true;
};
struct Query {
    MK::Snapshot Machine;
    MK::Task TaskDefinition;
    std::vector<double> InitialPositions;
    std::vector<Station> Stations;
    std::vector<MK::AxisConstraint> Constraints;
    // Same node slots and physical units as the machine. Fixed slots zero.
    // Planning limits in mm/radian and seconds. Fixed slots are zero.
    // Positive infinity means no acceleration/jerk or TCP feed constraint;
    // moving-axis velocity and step limits remain finite and positive.
    std::vector<double> MaximumVelocities,MaximumAccelerations,MaximumJerks,MaximumAxisSteps;
    double MaximumSeconds=30,IKTimeMsPerTarget=100;
    std::size_t IKSeedsPerTarget=2,IKCandidatesPerTarget=2,MaximumCandidatesPerStation=8;
    std::size_t MaximumGraphEdges=2000000,MaximumGraphSearchIterations=128;
    double PositionToleranceMm=.05,DirectionToleranceRad=1e-4;
    double PositionScaleMm=100,DirectionScale=1;
    // Each executable submove receives at least 2^(minimum+1)-1 interior FK
    // probes. The maximum also bounds adaptive interior-IK subdivision depth.
    std::size_t MinimumVerificationDepth=2,MaximumVerificationDepth=10,MaximumVerificationSamples=100000;
    // Frozen/worker-local callbacks, bounded and cooperatively cancellable.
    // State station index == size_t max denotes the current initial position.
    // An inserted interior state uses its original destination station index.
    std::function<Validation(std::size_t,const std::vector<double>&)> CheckState;
    // Edge 0 is initial->station0 (mandatory, even identical endpoints).
    // Edge i>0 is station(i-1)->station(i). Adaptive interior IK may replace a
    // coarse chord by multiple LINEAR physical-axis submoves following the
    // same ReferenceAt curve. This callback receives EACH actual submove and
    // the ORIGINAL edge index. Accepted certifies its ENTIRE physical motion,
    // not just endpoints/sample points; an unsafe discarded chord is never run.
    std::function<Validation(std::size_t,const std::vector<double>&,const std::vector<double>&)> CheckMotion;
    // Certifies the entire delivered polynomial, not its endpoint chord.
    std::function<Validation(const AxisSegment&)> CheckTrajectory;
    // Required exact intended TCP reference on edge i>0. The selected endpoint
    // directions let an owner express its allowed orientation interpolation.
    std::function<Reference(std::size_t,double,const MK::Vector&,const MK::Vector&)> ReferenceAt;
    // Optional worker-local observation; never owns scene/product state.
    std::function<void(const std::string&,std::size_t,std::size_t)> Progress;
};
struct Sample {
    // Initial approach sample has SourceStation=size_t max. Interpolation
    // samples reference the destination station and SourceFraction in [0,1].
    std::size_t SourceStation=std::numeric_limits<std::size_t>::max();
    std::string StationID,Role,SeedSource,BranchKey;
    double SourceFraction=0,TimeSeconds=0,PositionErrorMm=0,DirectionErrorRad=0;
    std::size_t SegmentIndex=std::numeric_limits<std::size_t>::max();
    bool DirectionChecked=true;
    std::vector<double> Positions;
    std::vector<double> Velocities,Accelerations;
    MK::Vector TCP{},Direction{};
};
enum class Status {Solved,NoCandidate,Rejected,Unresolved,Cancelled,BudgetExhausted,Unsupported};
struct Result {
    Status State=Status::Unresolved;
    // Failure/cancellation results publish NO playable partial samples.
    std::vector<Sample> Samples;
    std::vector<AxisSegment> Segments;
    std::string Coverage="FiniteCandidateGraph",InterpolationCoverage="SampledFK;CertifiedPolynomialDynamics",TimingQualification="VelocityAccelerationJerk",Diagnostic,Provider;
    double MaximumVelocityRatio=0,MaximumAccelerationRatio=0,MaximumJerkRatio=0;
    std::size_t StopKnots=0,BlendedKnots=0;bool ApproximateSolver=false;
    // Adaptive FK sampling is not a conservative continuous error bound. Even
    // Solved must not be promoted to physical machineReady/dynamics-qualified.
    bool MachineReady=false;
    bool CandidateSearchBudgetExhausted=false;
    std::size_t FailedStation=std::numeric_limits<std::size_t>::max(),FailedEdge=std::numeric_limits<std::size_t>::max();
    std::size_t IKCalls=0,IKSeeds=0,CandidateCount=0,StateChecks=0,MotionChecks=0,VerificationSamples=0;
    double ElapsedMs=0,DurationSeconds=0,MaximumPositionErrorMm=0,MaximumDirectionErrorRad=0;
};
}
namespace iCAX::GeometryServices {
class IMachineMotionPlanningService:public Services::IService {
public:
    virtual MachineMotion::Result Plan(const MachineMotion::Query&,
        const std::function<bool()>& cancelled={})const=0;
};
}
