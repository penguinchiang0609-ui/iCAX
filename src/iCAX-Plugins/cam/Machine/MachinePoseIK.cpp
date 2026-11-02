#include "MachinePoseIK.h"
#include <kdl/chainiksolverpos_nr_jl.hpp>
#include <kdl/chainiksolverpos_lma.hpp>
#include <kdl/chainiksolvervel_pinv.hpp>
#include <kdl/chainfksolverpos_recursive.hpp>
#include <kdl/chainjnttojacsolver.hpp>
#include <Eigen/Cholesky>
#include <Eigen/SVD>
#include <chrono>
namespace iCAX::CAM::Motion {
namespace {
KDL::Frame Frame(const Matrix& m){return {KDL::Rotation(m[0],m[1],m[2],m[4],m[5],m[6],m[8],m[9],m[10]),KDL::Vector(m[3]*.001,m[7]*.001,m[11]*.001)};}
void ValidateFrame(const Matrix& m){
 for(double value:m)if(!std::isfinite(value))throw std::invalid_argument("Nonfinite IK frame");
 if(std::abs(m[12])>1e-9||std::abs(m[13])>1e-9||std::abs(m[14])>1e-9||std::abs(m[15]-1)>1e-9)throw std::invalid_argument("Invalid IK frame row");
 for(int i=0;i<3;++i)for(int j=0;j<3;++j){double dot=0;for(int k=0;k<3;++k)dot+=m[k*4+i]*m[k*4+j];if(std::abs(dot-(i==j?1.:0.))>1e-7)throw std::invalid_argument("Nonrigid IK frame");}
 const double determinant=m[0]*(m[5]*m[10]-m[6]*m[9])-m[1]*(m[4]*m[10]-m[6]*m[8])+m[2]*(m[4]*m[9]-m[5]*m[8]);
 if(std::abs(determinant-1)>1e-7)throw std::invalid_argument("Invalid IK frame rotation");
}
}
PoseResidual MeasurePoseResidual(const Matrix& actual,const Matrix& target){
 ValidateFrame(actual);ValidateFrame(target);const auto error=KDL::diff(Frame(actual),Frame(target));return {error.vel.Norm()*1000,error.rot.Norm()};
}
PoseSolution SolvePoseKDL(const std::vector<Node>& nodes,int tip,const Matrix& target,const std::vector<double>& seed){
 const auto start=std::chrono::steady_clock::now();
 if(seed.size()!=nodes.size()||tip<0||tip>=static_cast<int>(nodes.size()))throw std::invalid_argument("Invalid IK chain");
 std::vector<int> path;for(int i=tip;i>=0;i=nodes[i].Parent){if(nodes[i].Parent>=i||nodes[i].Parent < -1)throw std::invalid_argument("Invalid IK parent");path.push_back(i);}
 std::reverse(path.begin(),path.end());KDL::Chain chain;std::vector<int> axes;
 for(int i:path){const auto& n=nodes[i];chain.addSegment(KDL::Segment(n.ID+"_origin",KDL::Joint(KDL::Joint::Fixed),Frame(n.Origin)));
  if(n.Type=="fixed")continue;
  if(n.Type!="prismatic"&&n.Type!="revolute"&&n.Type!="continuous")throw std::invalid_argument("Unsupported IK joint");
  if(!std::isfinite(seed[i])||!std::isfinite(n.Lower)||!std::isfinite(n.Upper)||n.Lower>n.Upper||seed[i]<n.Lower||seed[i]>n.Upper)throw std::invalid_argument("Invalid IK joint limits");
  const auto a=Unit(n.Axis);chain.addSegment(KDL::Segment(n.ID,KDL::Joint(n.ID,KDL::Vector::Zero(),KDL::Vector(a[0],a[1],a[2]),n.Type=="prismatic"?KDL::Joint::TransAxis:KDL::Joint::RotAxis)));axes.push_back(i);
 }
 if(axes.empty())throw std::invalid_argument("IK target has no movable ancestors");
 KDL::JntArray q(axes.size()),lo(axes.size()),hi(axes.size()),out(axes.size());
 for(size_t j=0;j<axes.size();++j){const auto i=axes[j];const double scale=nodes[i].Type=="prismatic"?.001:1.;q(j)=seed[i]*scale;lo(j)=nodes[i].Lower*scale;hi(j)=nodes[i].Upper*scale;}
 KDL::ChainFkSolverPos_recursive fk(chain);KDL::ChainIkSolverVel_pinv velocity(chain,1e-6,80);
 KDL::ChainIkSolverPos_NR_JL solver(chain,lo,hi,fk,velocity,80,1e-6);
 PoseSolution result;result.Positions=seed;
 // KDL LMA damping keeps a drag stable around wrist singularities. If its
 // unconstrained result violates a physical limit, use KDL's bounded solver.
 Eigen::Matrix<double,6,1> weights;weights.setOnes();
 KDL::ChainIkSolverPos_LMA damped(chain,weights,1e-7,80,1e-12);
 result.Code=damped.CartToJnt(q,Frame(target),out);
 bool bounded=result.Code>=0;for(unsigned j=0;j<out.rows();++j)bounded=bounded&&std::isfinite(out(j))&&out(j)>=lo(j)&&out(j)<=hi(j);
 if(!bounded)result.Code=solver.CartToJnt(q,Frame(target),out);
 KDL::Frame reached;fk.JntToCart(out,reached);const auto error=KDL::diff(reached,Frame(target));
 result.PositionError=error.vel.Norm()*1000;result.RotationError=error.rot.Norm();
 result.Reached=result.Code>=0&&result.PositionError<=.01&&result.RotationError<=1e-5;
 for(size_t j=0;j<axes.size();++j){const auto i=axes[j];const double value=out(j)*(nodes[i].Type=="prismatic"?1000.:1.);if(!std::isfinite(value)||value<nodes[i].Lower-1e-9||value>nodes[i].Upper+1e-9)result.Reached=false;result.Positions[i]=value;}
 result.SolveMilliseconds=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();return result;
}
PoseSolution SolveClosestPoseKDL(const std::vector<Node>& nodes,int tip,const Matrix& target,const std::vector<double>& seed,const PoseSolveOptions& options){
 const auto start=std::chrono::steady_clock::now();
 if(seed.size()!=nodes.size()||tip<0||tip>=static_cast<int>(nodes.size()))throw std::invalid_argument("Invalid IK chain");
 if(!options.MaxIterations||!std::isfinite(options.MaxMilliseconds)||options.MaxMilliseconds<=0||!std::isfinite(options.OrientationScaleMm)||options.OrientationScaleMm<=0)throw std::invalid_argument("Invalid IK solve budget or metric");
 for(double value:seed)if(!std::isfinite(value))throw std::invalid_argument("Nonfinite IK seed");
 ValidateFrame(target);
 std::vector<int> path;
 for(int i=tip;i>=0;i=nodes[i].Parent){if(nodes[i].Parent>=i||nodes[i].Parent < -1)throw std::invalid_argument("Invalid IK parent");path.push_back(i);}
 std::reverse(path.begin(),path.end());KDL::Chain chain;std::vector<int> axes;
 for(int i:path){
  const auto& node=nodes[i];ValidateFrame(node.Origin);chain.addSegment(KDL::Segment(node.ID+"_origin",KDL::Joint(KDL::Joint::Fixed),Frame(node.Origin)));
  if(node.Type=="fixed")continue;
  if(node.Type!="prismatic"&&node.Type!="revolute"&&node.Type!="continuous")throw std::invalid_argument("Unsupported IK joint");
  if(!std::isfinite(node.Lower)||!std::isfinite(node.Upper)||node.Lower>node.Upper||seed[i]<node.Lower||seed[i]>node.Upper)throw std::invalid_argument("Invalid IK joint limits");
  const auto axis=Unit(node.Axis);chain.addSegment(KDL::Segment(node.ID,KDL::Joint(node.ID,KDL::Vector::Zero(),KDL::Vector(axis[0],axis[1],axis[2]),node.Type=="prismatic"?KDL::Joint::TransAxis:KDL::Joint::RotAxis)));axes.push_back(i);
 }
 if(axes.empty())throw std::invalid_argument("IK target has no movable ancestors");
 const auto count=static_cast<unsigned>(axes.size());
 KDL::JntArray q(count),lo(count),hi(count);Eigen::VectorXd stepLimit(count);
 for(unsigned j=0;j<count;++j){const auto i=axes[j];const bool linear=nodes[i].Type=="prismatic";const double scale=linear?.001:1.;q(j)=seed[i]*scale;lo(j)=nodes[i].Lower*scale;hi(j)=nodes[i].Upper*scale;stepLimit(j)=linear?.1:.15;}
 KDL::ChainFkSolverPos_recursive fk(chain);KDL::ChainJntToJacSolver jacobianSolver(chain);KDL::Jacobian jacobian(count);
 const auto goal=Frame(target);const double rotationScale=options.OrientationScaleMm*.001;
 // Project each gradient onto a unit task-space tangent. Its magnitude then
 // has length units, independent of whether its joint uses metres or radians.
 // 0.0001 mm / 1e-7 rad is 100 times finer than exact reachability. Requiring
 // nanometre-scale stationarity made nonzero underactuated residuals stall at
 // the floating-point resolution of their squared cost instead of converging.
 const double stationaryTolerance=std::min(1e-7,rotationScale*1e-7);
 PoseSolution result;result.Positions=seed;result.Code=-1;
 const auto elapsed=[&](){return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();};
 const auto interrupted=[&](){
  if(options.IsCancelled&&options.IsCancelled()){result.Cancelled=true;result.Code=-3;return true;}
  if(elapsed()>=options.MaxMilliseconds){result.BudgetExhausted=true;result.Code=-2;return true;}
  return false;
 };
 using ErrorVector=Eigen::Matrix<double,6,1>;
 const auto evaluate=[&](const KDL::JntArray& joints,ErrorVector& error,PoseResidual& residual){
  KDL::Frame actual;if(fk.JntToCart(joints,actual)<0)return false;
  const auto difference=KDL::diff(actual,goal);residual={difference.vel.Norm()*1000,difference.rot.Norm()};
  for(int k=0;k<3;++k){error(k)=difference.vel(k);error(k+3)=rotationScale*difference.rot(k);}
  return error.allFinite()&&std::isfinite(error.squaredNorm())&&std::isfinite(residual.PositionError)&&std::isfinite(residual.RotationError);
 };
 ErrorVector error;PoseResidual residual;
 if(!evaluate(q,error,residual))throw std::invalid_argument("Invalid IK residual");
 result.CandidateValid=true;double cost=error.squaredNorm();const double initialCost=cost;double damping=1e-6;
 const auto exact=[&](){return residual.PositionError<=.01&&residual.RotationError<=1e-5;};
 for(unsigned iteration=0;iteration<options.MaxIterations;++iteration){
  if(interrupted())break;
  result.Iterations=iteration+1;
  if(exact()){result.Converged=true;result.Code=0;break;}
  if(jacobianSolver.JntToJac(q,jacobian)<0)break;
  Eigen::Matrix<double,6,Eigen::Dynamic> jac=jacobian.data;jac.bottomRows<3>()*=rotationScale;
  if(!jac.allFinite())break;
  Eigen::VectorXd gradient=jac.transpose()*error;double projectedNorm=0;
  for(unsigned j=0;j<count;++j){
   const bool blocked=lo(j)==hi(j)||(q(j)<=lo(j)+1e-12&&gradient(j)<0)||(q(j)>=hi(j)-1e-12&&gradient(j)>0);
   if(blocked){jac.col(j).setZero();gradient(j)=0;}
   const double tangentLength=jac.col(j).norm();
   if(tangentLength>0)projectedNorm=std::max(projectedNorm,std::abs(gradient(j))/tangentLength);
  }
  if(projectedNorm<=stationaryTolerance){
   // At a straight-chain singularity a first-order stationary seed can be a
   // saddle. Test small, deterministic nearby bends, including Jacobian null
   // directions, before accepting stationarity. Never restart on a far branch.
   std::vector<Eigen::VectorXd> probes;
   for(unsigned j=0;j<count;++j)if(nodes[axes[j]].Type!="prismatic"&&lo(j)!=hi(j)){auto direction=Eigen::VectorXd::Zero(count).eval();direction(j)=.02;probes.push_back(direction);}
   Eigen::JacobiSVD<Eigen::Matrix<double,6,Eigen::Dynamic>> svd(jac,Eigen::ComputeFullV);
   for(unsigned j=0;j<count;++j)if(j>=static_cast<unsigned>(svd.singularValues().size())||svd.singularValues()(j)<1e-5){
    auto direction=svd.matrixV().col(j).eval();double factor=1;
    for(unsigned k=0;k<count;++k)factor=std::max(factor,std::abs(direction(k))/(nodes[axes[k]].Type=="prismatic"?.005:.02));
    direction/=factor;probes.push_back(direction);
   }
   bool escaped=false;
   for(const auto& probe:probes){
    for(double sign:{1.,-1.}){
     if(interrupted())break;
     KDL::JntArray trial=q;for(unsigned j=0;j<count;++j)trial(j)=std::clamp(q(j)+sign*probe(j),lo(j),hi(j));
     ErrorVector trialError;PoseResidual trialResidual;
     if(evaluate(trial,trialError,trialResidual)&&trialError.squaredNorm()<cost-std::max(1e-18,cost*1e-12)){q=trial;error=trialError;residual=trialResidual;cost=error.squaredNorm();escaped=true;break;}
    }
    if(escaped||result.Cancelled||result.BudgetExhausted)break;
   }
   if(result.Cancelled||result.BudgetExhausted)break;
   if(escaped)continue;
   result.Converged=true;result.Code=1;break;
  }
  // The task is six-dimensional regardless of the number of chain joints.
  // A damped dual solve remains bounded and well-conditioned at singularities.
  Eigen::Matrix<double,6,6> normal=jac*jac.transpose();normal.diagonal().array()+=damping;
  Eigen::VectorXd step=jac.transpose()*normal.ldlt().solve(error);
  if(!step.allFinite())break;
  const auto limitStep=[&](Eigen::VectorXd& value){double factor=1;for(unsigned j=0;j<count;++j)factor=std::max(factor,std::abs(value(j))/stepLimit(j));value/=factor;};
  limitStep(step);bool accepted=false;
  // Clamping can change a coupled Newton direction; a projected-gradient
  // fallback provides descent without allowing an active limit to drift.
  for(int attempt=0;attempt<2&&!accepted;++attempt){
   if(attempt){for(unsigned j=0;j<count;++j)step(j)=gradient(j)/std::max(.001,jac.col(j).squaredNorm());limitStep(step);}
   for(double alpha=1;alpha>=1./8192;alpha*=.5){
    if(interrupted())break;
    KDL::JntArray trial=q;for(unsigned j=0;j<count;++j)trial(j)=std::clamp(q(j)+alpha*step(j),lo(j),hi(j));
    ErrorVector trialError;PoseResidual trialResidual;
    if(evaluate(trial,trialError,trialResidual)&&trialError.squaredNorm()<cost-std::max(1e-20,cost*1e-14)){
     q=trial;error=trialError;residual=trialResidual;cost=error.squaredNorm();accepted=true;damping=std::max(1e-10,damping*.3);break;
    }
   }
   if(result.Cancelled||result.BudgetExhausted)break;
  }
  if(result.Cancelled||result.BudgetExhausted)break;
  if(!accepted){damping=std::min(1e8,damping*100);if(damping>=1e8)break;}
 }
 if(!result.Converged&&!result.Cancelled&&!result.BudgetExhausted&&result.Iterations>=options.MaxIterations){result.BudgetExhausted=true;result.Code=-2;}
 // Re-evaluate the actual returned joints rather than reporting a requested
 // target or an intermediate Newton estimate. Other branches remain the seed.
 result.CandidateValid=evaluate(q,error,residual);
 for(unsigned j=0;j<count;++j){const auto i=axes[j];result.Positions[i]=q(j)*(nodes[i].Type=="prismatic"?1000.:1.);if(!std::isfinite(result.Positions[i])||result.Positions[i]<nodes[i].Lower-1e-9||result.Positions[i]>nodes[i].Upper+1e-9)result.CandidateValid=false;}
 result.PositionError=residual.PositionError;result.RotationError=residual.RotationError;
 result.Improved=result.CandidateValid&&cost<initialCost-std::max(1e-20,initialCost*1e-14);
 result.Reached=result.CandidateValid&&!result.Cancelled&&exact();
 // Exact FK verification is itself a convergence certificate even if the last
 // accepted step consumed the final iteration. Time/cancellation remain explicit.
 if(result.Reached&&!result.BudgetExhausted){result.Converged=true;result.Code=0;}
 result.SolveMilliseconds=elapsed();return result;
}
}
