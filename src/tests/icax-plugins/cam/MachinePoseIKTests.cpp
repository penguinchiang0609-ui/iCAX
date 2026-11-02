#include "../../../iCAX-Plugins/cam/Machine/MachinePoseIK.h"
#include <cassert>
#include <iostream>
#include <limits>
#ifdef _MSC_VER
#include <crtdbg.h>
#include <cstdlib>
#endif
using namespace iCAX::CAM::Motion;
namespace {
PoseSolveOptions TestBudget(){PoseSolveOptions options;options.MaxMilliseconds=1000;return options;}
void VerifyResidual(const std::vector<Node>& nodes,int tip,const Matrix& target,const PoseSolution& solution){
 assert(solution.CandidateValid);const auto residual=MeasurePoseResidual(Forward(nodes,solution.Positions)[tip],target);
 assert(std::abs(residual.PositionError-solution.PositionError)<1e-7);assert(std::abs(residual.RotationError-solution.RotationError)<1e-9);
 for(size_t i=0;i<nodes.size();++i)if(nodes[i].Type!="fixed"){assert(solution.Positions[i]>=nodes[i].Lower-1e-9);assert(solution.Positions[i]<=nodes[i].Upper+1e-9);}
}
template<class F> void Invalid(F&& operation){bool rejected=false;try{operation();}catch(const std::invalid_argument&){rejected=true;}assert(rejected);}
void TestTCCradle(){
 std::vector<Node> nodes(3);nodes[1].Parent=0;nodes[1].Type="revolute";nodes[1].Origin[11]=1000;nodes[1].Axis={1,0,0};nodes[1].Lower=-1.5;nodes[1].Upper=1.5;
 nodes[2].Parent=1;nodes[2].Type="revolute";nodes[2].Origin[11]=-80;nodes[2].Axis={0,0,1};nodes[2].Lower=-6.3;nodes[2].Upper=6.3;
 const std::vector<double> seed{0,-.06716500157695485,.414141095030357};const auto current=Forward(nodes,seed)[2];
 for(const auto& axis:std::vector<Vector>{{.526,-.789,.316},{0,1,0},{.3,.5,.8},{1,0,0},{0,0,1}}){
  Node rotation;rotation.Type="revolute";rotation.Axis=axis;auto target=Multiply(Joint(rotation,.9),current);for(int row=0;row<3;++row)target[row*4+3]=current[row*4+3];
  const auto solution=SolveClosestPoseKDL(nodes,2,target,seed);VerifyResidual(nodes,2,target,solution);
  assert(solution.Converged&&!solution.Reached&&solution.Improved&&!solution.BudgetExhausted&&!solution.Cancelled);
  const auto objective=[&](const std::vector<double>& positions){const auto residual=MeasurePoseResidual(Forward(nodes,positions)[2],target);return residual.PositionError*residual.PositionError*1e-6+residual.RotationError*residual.RotationError;};
  double gradient=0;for(int joint=1;joint<=2;++joint){auto plus=solution.Positions,minus=solution.Positions;plus[joint]+=1e-5;minus[joint]-=1e-5;gradient=std::max(gradient,std::abs(objective(plus)-objective(minus))/2e-5);}
  // Independently perturb FK joints, including coupled directions and scales
  // above/below the convergence resolution. No nearby pose may improve the
  // metre/radian squared task error by more than two 1e-7-unit components.
  const double cost=objective(solution.Positions);assert(cost<objective(seed));double nearbyImprovement=0;
  for(double radius:{1e-7,1e-6,1e-5,1e-4,1e-3,.02})for(int a=-1;a<=1;++a)for(int c=-1;c<=1;++c){
   auto trial=solution.Positions;trial[1]+=a*radius;trial[2]+=c*radius;nearbyImprovement=std::max(nearbyImprovement,cost-objective(trial));
  }
  assert(gradient<2.1e-7);assert(nearbyImprovement<=2e-14);
  const auto repeated=SolveClosestPoseKDL(nodes,2,target,solution.Positions);assert(repeated.Converged&&!repeated.Improved&&repeated.Positions==solution.Positions);
  std::cout.precision(16);std::cout<<"TC 2R axis ["<<axis[0]<<","<<axis[1]<<","<<axis[2]<<"] converged="<<solution.Converged<<" code="<<solution.Code<<" iterations="<<solution.Iterations<<" ms="<<solution.SolveMilliseconds<<" pos="<<solution.PositionError<<" rot="<<solution.RotationError<<" gradient="<<gradient<<" nearbyImprovement="<<nearbyImprovement<<" A="<<solution.Positions[1]<<" C="<<solution.Positions[2]<<"\n";
 }
 std::cout<<"PASS TC cradle: supplied real seed, arbitrary/world X/Y/Z rotation, true FK residual, independent nearby improvement <= 2e-14, repeat stability, default 12 ms budget\n";
}
void TestClosest(){
 // A one-axis slide can approach an unreachable sideways target. Only the
 // selected ancestor chain changes; child and separate-branch values survive.
 std::vector<Node> slides(4);slides[1].Parent=0;slides[1].Type="prismatic";slides[1].Lower=-100;slides[1].Upper=100;
 slides[2].Parent=1;slides[2].Type="prismatic";slides[2].Axis={0,1,0};slides[2].Lower=-100;slides[2].Upper=100;
 slides[3]=slides[2];slides[3].Parent=0;
 const std::vector<double> seed{0,20,13,7};auto target=Identity();target[3]=80;target[7]=500;
 const auto before=Forward(slides,seed);const auto side=SolveClosestPoseKDL(slides,1,target,seed,TestBudget());
 assert(side.Converged&&!side.Reached&&side.Improved&&!side.BudgetExhausted);assert(std::abs(side.Positions[1]-80)<1e-4);
 assert(side.Positions[2]==13&&side.Positions[3]==7);assert(std::abs(side.PositionError-500)<1e-7);VerifyResidual(slides,1,target,side);
 assert(Forward(slides,seed)==before);
 auto stationarySeed=seed;stationarySeed[1]=80;const auto stationary=SolveClosestPoseKDL(slides,1,target,stationarySeed,TestBudget());
 assert(stationary.Converged&&stationary.CandidateValid&&!stationary.Improved&&!stationary.Reached);
 // A distant pointer projects to the travel boundary, regardless of its
 // distance from the last mouse sample. Strict pose IK continues to reject it.
 target=Identity();target[3]=10000;
 const auto far=SolveClosestPoseKDL(slides,1,target,seed,TestBudget());
 assert(far.Converged&&!far.Reached&&far.Improved);assert(std::abs(far.Positions[1]-100)<1e-7);assert(std::abs(far.PositionError-9900)<1e-7);
 VerifyResidual(slides,1,target,far);assert(!SolvePoseKDL(slides,1,target,seed).Reached);
 const auto defaultFar=SolveClosestPoseKDL(slides,1,target,seed);assert(defaultFar.Converged&&!defaultFar.BudgetExhausted);
 slides[1].Lower=slides[1].Upper=20;
 const auto locked=SolveClosestPoseKDL(slides,1,target,seed,TestBudget());
 assert(locked.Converged&&!locked.Improved&&locked.Positions==seed);VerifyResidual(slides,1,target,locked);
 slides[1].Lower=-100;slides[1].Upper=100;
 PoseSolveOptions shortBudget=TestBudget();shortBudget.MaxIterations=1;
 const auto exhausted=SolveClosestPoseKDL(slides,1,target,seed,shortBudget);
 assert(exhausted.CandidateValid&&exhausted.BudgetExhausted&&!exhausted.Converged&&!exhausted.Reached);VerifyResidual(slides,1,target,exhausted);
 shortBudget=TestBudget();shortBudget.MaxMilliseconds=1e-9;
 const auto timed=SolveClosestPoseKDL(slides,1,target,seed,shortBudget);
 assert(timed.CandidateValid&&timed.BudgetExhausted&&!timed.Converged&&timed.Iterations==0);
 shortBudget=TestBudget();shortBudget.IsCancelled=[](){return true;};
 const auto cancelled=SolveClosestPoseKDL(slides,1,target,seed,shortBudget);
 assert(cancelled.CandidateValid&&cancelled.Cancelled&&!cancelled.Converged&&!cancelled.Reached&&cancelled.Positions==seed);
 int polls=0;shortBudget.IsCancelled=[&](){return ++polls>=3;};
 const auto cancelledDuring=SolveClosestPoseKDL(slides,1,target,seed,shortBudget);
 assert(cancelledDuring.Cancelled&&!cancelledDuring.Converged&&!cancelledDuring.Reached);VerifyResidual(slides,1,target,cancelledDuring);
 auto illegalSeed=seed;illegalSeed[3]=std::numeric_limits<double>::quiet_NaN();Invalid([&](){SolveClosestPoseKDL(slides,1,target,illegalSeed);});
 auto illegalTarget=target;illegalTarget[0]=2;Invalid([&](){SolveClosestPoseKDL(slides,1,illegalTarget,seed);});
 illegalTarget=target;illegalTarget[15]=0;Invalid([&](){MeasurePoseResidual(illegalTarget,target);});
 auto illegalNodes=slides;illegalNodes[1].Axis={0,0,0};Invalid([&](){SolveClosestPoseKDL(illegalNodes,1,target,seed);});
 illegalNodes=slides;illegalNodes[1].Parent=1;Invalid([&](){SolveClosestPoseKDL(illegalNodes,1,target,seed);});
 shortBudget=TestBudget();shortBudget.OrientationScaleMm=0;Invalid([&](){SolveClosestPoseKDL(slides,1,target,seed,shortBudget);});
 shortBudget=TestBudget();shortBudget.MaxIterations=0;Invalid([&](){SolveClosestPoseKDL(slides,1,target,seed,shortBudget);});
 // Rotary limit and full-pose residual are both enforced, including roll.
 std::vector<Node> rotary(3);rotary[1].Parent=0;rotary[1].Type="revolute";rotary[1].Axis={0,0,1};rotary[1].Lower=-.5;rotary[1].Upper=.5;rotary[2].Parent=1;rotary[2].Origin[3]=100;
 std::vector<double> rotarySeed(3),rotaryGoal{0,.8,0};target=Forward(rotary,rotaryGoal)[2];
 const auto bounded=SolveClosestPoseKDL(rotary,2,target,rotarySeed,TestBudget());
 assert(bounded.Converged&&!bounded.Reached&&std::abs(bounded.Positions[1]-.5)<1e-7);assert(std::abs(bounded.RotationError-.3)<1e-8);VerifyResidual(rotary,2,target,bounded);
 // Inconsistent position/orientation requests minimize the declared 1000 mm
 // per radian metric, rather than discarding one orientation component.
 rotaryGoal[1]=.4;target=Forward(rotary,rotaryGoal)[2];rotaryGoal[1]=-.2;const auto wantedRotation=Forward(rotary,rotaryGoal)[2];
 for(int row=0;row<3;++row)for(int column=0;column<3;++column)target[row*4+column]=wantedRotation[row*4+column];
 const auto compromise=SolveClosestPoseKDL(rotary,2,target,rotarySeed,TestBudget());
 assert(compromise.Converged&&!compromise.Reached&&compromise.Improved);const double q=compromise.Positions[1];assert(std::abs(q+.2+.01*std::sin(q-.4))<1e-8);VerifyResidual(rotary,2,target,compromise);
 const auto defaultCompromise=SolveClosestPoseKDL(rotary,2,target,rotarySeed);assert(defaultCompromise.Converged&&!defaultCompromise.BudgetExhausted);
 // A straight three-link chain needs a small local null-space bend to escape
 // its stationary saddle, without trying unrelated distant joint branches.
 std::vector<Node> arm(4);for(int i=1;i<4;++i){arm[i].Parent=i-1;arm[i].Type="revolute";arm[i].Axis={0,0,1};arm[i].Lower=-1.5;arm[i].Upper=1.5;if(i>1)arm[i].Origin[3]=1000;}
 std::vector<double> armSeed(4);target=Identity();target[3]=1900;
 const auto bend=SolveClosestPoseKDL(arm,3,target,armSeed,TestBudget());
 assert(bend.Reached&&bend.Converged&&bend.Improved);VerifyResidual(arm,3,target,bend);
 const auto defaultBend=SolveClosestPoseKDL(arm,3,target,armSeed);assert(defaultBend.Converged&&!defaultBend.BudgetExhausted);VerifyResidual(arm,3,target,defaultBend);
 std::cout<<"Default 12 ms budget (Debug): far slide "<<defaultFar.SolveMilliseconds<<" ms/"<<defaultFar.Iterations<<" iterations; rotary compromise "<<defaultCompromise.SolveMilliseconds<<" ms/"<<defaultCompromise.Iterations<<"; singular bend "<<defaultBend.SolveMilliseconds<<" ms/"<<defaultBend.Iterations<<"\n";
 std::cout<<"PASS closest pose: travel/rotary limits, sideways/stationary targets, full-pose metric, branch isolation, singular bend, cancellation, budgets, input validation\n";
}
}
int main(){
#ifdef _MSC_VER
 _CrtSetReportMode(_CRT_ASSERT,_CRTDBG_MODE_FILE);_CrtSetReportFile(_CRT_ASSERT,_CRTDBG_FILE_STDERR);_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);
#endif
 std::vector<Node> nodes(8);for(int i=0;i<8;++i)nodes[i].ID=std::to_string(i);
 nodes[0].Origin={0,-1,0,200,1,0,0,-50,0,0,1,70,0,0,0,1};
 for(int i=1;i<=3;++i){nodes[i].Parent=i-1;nodes[i].Type="prismatic";nodes[i].Axis={0,0,0};nodes[i].Axis[i-1]=1;nodes[i].Lower=-500;nodes[i].Upper=500;}
 for(int i=4;i<=5;++i){nodes[i].Parent=i-1;nodes[i].Type="revolute";nodes[i].Axis=i==4?Vector{1,0,0}:Vector{0,1,0};nodes[i].Lower=-1;nodes[i].Upper=1;}
 nodes[6].Parent=5;nodes[6].Origin[11]=-80;
 nodes[7].Parent=0;nodes[7].Type="prismatic";nodes[7].Axis={0,0,1};nodes[7].Lower=-100;nodes[7].Upper=100;
 std::vector<double> seed(8),goal{0,25,-10,30,.1,-.15,0,0};seed[7]=7;goal[7]=7;
 auto target=Forward(nodes,goal)[6];const auto s=SolvePoseKDL(nodes,6,target,seed);assert(s.Reached);assert(s.PositionError<.01&&s.RotationError<1e-5);assert(s.Positions[7]==7);
 target[3]+=10000;const auto unreachable=SolvePoseKDL(nodes,6,target,seed);assert(!unreachable.Reached);
 // Six axes and a translated, rotated base; both FK and full orientation must agree.
 nodes[6].Type="revolute";nodes[6].Axis={0,0,1};nodes[6].Lower=-1;nodes[6].Upper=1;goal[6]=.2;
 target=Forward(nodes,goal)[6];const auto six=SolvePoseKDL(nodes,6,target,seed);assert(six.Reached&&six.PositionError<.01&&six.RotationError<1e-5);assert(six.Positions[7]==7);
 const auto closestSix=SolveClosestPoseKDL(nodes,6,target,seed,TestBudget());assert(closestSix.Reached&&closestSix.Converged);VerifyResidual(nodes,6,target,closestSix);assert(closestSix.Positions[7]==7);
 const auto defaultSix=SolveClosestPoseKDL(nodes,6,target,seed);assert(defaultSix.Reached&&defaultSix.Converged&&!defaultSix.BudgetExhausted);std::cout<<"Default 12 ms budget (Debug): six-axis "<<defaultSix.SolveMilliseconds<<" ms/"<<defaultSix.Iterations<<" iterations\n";
 nodes[2].Lower=nodes[2].Upper=0;const auto limited=SolvePoseKDL(nodes,6,target,seed);assert(!limited.Reached);
 std::cout<<"PASS KDL: mixed five/six axes, rotated base, fixed tail offset, branch isolation, unreachable and joint limits\n";
 TestClosest();
 TestTCCradle();
}
