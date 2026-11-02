#pragma once
#include "MachineExport.h"
#include "MachineMotion.h"
#include <functional>
namespace iCAX::CAM::Motion {
// Neutral full-pose IK. mm/radians and row-major frames match MachineMotion.
struct PoseSolution {
    bool Reached=false;
    std::vector<double> Positions;
    double PositionError=0,RotationError=0,SolveMilliseconds=0;
    int Code=0;
    // A finite bounded candidate is distinct from exact reachability and from
    // termination at a local constrained stationary pose. Cancelled/budget-limited
    // searches must not be reported as converged.
    bool CandidateValid=false,Converged=false,Improved=false;
    bool Cancelled=false,BudgetExhausted=false;
    unsigned Iterations=0;
};
struct PoseSolveOptions {
    unsigned MaxIterations=96;
    double MaxMilliseconds=12;
    // Millimetres per radian; defines the full-pose least-squares metric.
    double OrientationScaleMm=1000;
    std::function<bool()> IsCancelled;
};
struct PoseResidual {double PositionError=0,RotationError=0;};
_MACHINE_EXP PoseResidual MeasurePoseResidual(const Matrix& actual,const Matrix& target);
_MACHINE_EXP PoseSolution SolvePoseKDL(const std::vector<Node>& nodes,int tip,const Matrix& target,const std::vector<double>& seed);
// Warm-started local closest-pose search; only the tip's ancestors can move.
// Reached retains the strict 0.01 mm / 1e-5 rad meaning. Code is 0 for exact,
// 1 for a constrained stationary pose, -2 for budget, -3 for cancellation,
// and -1 when numerical progress cannot be certified.
_MACHINE_EXP PoseSolution SolveClosestPoseKDL(const std::vector<Node>& nodes,int tip,const Matrix& target,const std::vector<double>& seed,const PoseSolveOptions& options={});
}
