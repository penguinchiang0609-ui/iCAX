namespace orientation_evaluation {
using namespace iCAX::GeometryServices;
using ompl_evaluation::V;using ompl_evaluation::Add;using ompl_evaluation::Sub;using ompl_evaluation::Scale;
using ompl_evaluation::Unit;using ompl_evaluation::Length;using ompl_evaluation::Clock;using ompl_evaluation::Ms;
constexpr double Pi=ompl_evaluation::Pi;
double Angle(V a,V b){const auto d=std::clamp(a[0]*b[0]+a[1]*b[1]+a[2]*b[2],-1.,1.);return std::atan2(Length({a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]}),d);}
V Tilt(double degrees){const double t=degrees*Pi/180;return {0,std::sin(t),std::cos(t)};}
V Slerp(V a,V b,double t){const auto angle=Angle(a,b);if(angle<1e-10)return Unit(Add(Scale(a,1-t),Scale(b,t)));return Unit(Add(Scale(a,std::sin((1-t)*angle)/std::sin(angle)),Scale(b,std::sin(t*angle)/std::sin(angle))));}
CollisionTransform Pose(V p,V n){gp_Trsf pose;pose.SetRotation(gp_Quaternion(gp_Vec(0,0,1),gp_Vec(n[0],n[1],n[2])));pose.SetTranslationPart(gp_Vec(p[0],p[1],p[2]));
    CollisionTransform out{0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1};for(int i=0;i<3;++i)for(int j=0;j<4;++j)out[i*4+j]=pose.Value(i+1,j+1);return out;}
PathOrientationQuery Basic(std::size_t n){PathOrientationQuery q;q.MaximumSeconds=5;q.MaximumDirectionStepRadians=30*Pi/180;q.EditableRanges={{0,n-1}};
    for(std::size_t i=0;i<n;++i){OrientationWaypoint p;p.Position={static_cast<double>(i),0,0};p.MaximumDeviationRadians=40*Pi/180;q.Waypoints.push_back(p);}
    q.CheckState=[](auto,const auto&){return true;};q.CheckMotion=[](auto,const auto&,const auto&){return true;};return q;}
struct LinearCoalValidator {
    std::shared_ptr<ICollisionDistanceService> Service;CollisionDistanceModelRef Fixed,Head;
    double Radius=8.01,Clearance=.05;std::size_t Calls=0;
    double Distance(V p,V n){++Calls;return Service->LowerBound(Fixed,Head,Pose(p,n),Clearance+1e-6).LowerBound;}
    bool Motion(V a,V b,V na,V nb,int depth=0){const auto p=Scale(Add(a,b),.5),n=Slerp(na,nb,.5);const auto distance=Distance(p,n);
        if(distance<=Clearance+1e-6)return false;const auto movement=Length(Sub(a,b))*.5+2*Radius*std::sin(Angle(na,nb)*.25);
        if(distance-movement>Clearance+1e-6)return true;
        return depth<18&&Motion(a,p,na,n,depth+1)&&Motion(p,b,n,nb,depth+1);}
};
V Forward(bool ac,double tilt,double c){return ac?V{std::sin(tilt)*std::sin(c),-std::sin(tilt)*std::cos(c),std::cos(tilt)}:V{std::sin(tilt)*std::cos(c),std::sin(tilt)*std::sin(c),std::cos(tilt)};}
}
TEST(PathOrientation, ChoosesASequenceThatAvoidsTheGreedyDeadEnd)
{
    using namespace orientation_evaluation;Scene scene;auto q=Basic(3);q.MaximumDirectionStepRadians=25*Pi/180;
    q.Waypoints[1].Candidates={PathOrientation{Tilt(20),{}}};q.Waypoints[2].Original.Direction=Tilt(40);
    const auto r=scene.Services().Resolve<IPathOrientationPlanningService>()->Plan(q);
    ASSERT_EQ("Solved",r.Status);ASSERT_EQ(3u,r.Orientations.size());EXPECT_NEAR(20*Pi/180,Angle(q.Waypoints[1].Original.Direction,r.Orientations[1].Direction),1e-12);
    EXPECT_EQ(q.Waypoints.front().Original.Direction,r.Orientations.front().Direction);EXPECT_EQ(q.Waypoints.back().Original.Direction,r.Orientations.back().Direction);
    EXPECT_EQ((V{1,0,0}),q.Waypoints[1].Position);
}
TEST(PathOrientation, LocalRangesAndLockedPointsAreHardConstraintsAndFailureIsAtomic)
{
    using namespace orientation_evaluation;Scene scene;auto q=Basic(10);q.EditableRanges={{1,7}};q.Waypoints[3].Locked=true;
    for(auto& p:q.Waypoints)p.Candidates={PathOrientation{Tilt(20),{}}};
    q.CheckState=[](auto i,const auto& p){return i!=4||Angle(p.Direction,Tilt(20))<1e-9;};
    const auto service=scene.Services().Resolve<IPathOrientationPlanningService>();auto r=service->Plan(q);ASSERT_EQ("Solved",r.Status);
    for(const auto i:{0,1,3,7,8,9})EXPECT_EQ(q.Waypoints[i].Original.Direction,r.Orientations[i].Direction);
    EXPECT_EQ(Tilt(20),r.Orientations[4].Direction);EXPECT_EQ(Tilt(0),q.Waypoints[4].Original.Direction);
    q.Waypoints[4].Locked=true;r=service->Plan(q);EXPECT_EQ("NoSolution",r.Status);EXPECT_TRUE(r.Orientations.empty());
    q.Waypoints[4].Locked=false;q.EditableRanges={{0,2},{6,9}};r=service->Plan(q);EXPECT_EQ("NoSolution",r.Status);EXPECT_TRUE(r.Orientations.empty());
}
TEST(PathOrientation, CoalRejectsAnInteriorCollisionAndFindsAnOrientationWithoutMovingThePath)
{
    using namespace orientation_evaluation;Scene scene;auto q=Basic(3);LinearCoalValidator v;v.Service=scene.Services().Resolve<ICollisionDistanceService>();
    v.Fixed=v.Service->PrepareBoxes({CollisionBox{{.5,0,4},{.04,.15,.3}}});v.Head=v.Service->PrepareFrusta({{0,8,.1,.1}});
    q.Waypoints[1].Candidates={PathOrientation{Tilt(20),{}}};
    q.CheckState=[&](auto i,const auto& p){return v.Distance(q.Waypoints[i].Position,p.Direction)>v.Clearance+1e-6;};
    q.CheckMotion=[&](auto i,const auto& a,const auto& b){return v.Motion(q.Waypoints[i].Position,q.Waypoints[i+1].Position,a.Direction,b.Direction);};
    ASSERT_TRUE(q.CheckState(0,q.Waypoints[0].Original));ASSERT_TRUE(q.CheckState(1,q.Waypoints[1].Original));ASSERT_FALSE(q.CheckMotion(0,q.Waypoints[0].Original,q.Waypoints[1].Original));
    const auto r=scene.Services().Resolve<IPathOrientationPlanningService>()->Plan(q);ASSERT_EQ("Solved",r.Status);EXPECT_GT(r.GraphBuilds,1u);EXPECT_EQ(Tilt(20),r.Orientations[1].Direction);
    for(std::size_t i=0;i<r.Orientations.size();++i){EXPECT_TRUE(q.CheckState(i,r.Orientations[i]));if(i)EXPECT_TRUE(q.CheckMotion(i-1,r.Orientations[i-1],r.Orientations[i]));}
}
TEST(PathOrientation, ACAndBCAxesKeepThePhysicalUnwrappedBranchAcrossTheCSeam)
{
    using namespace orientation_evaluation;Scene scene;const auto service=scene.Services().Resolve<IPathOrientationPlanningService>();
    // Explicit reference kinematics: Rz(C)*Rx(A)*Z or Rz(C)*Ry(B)*Z.
    // This is an analytic head test, not qualification of an installed machine.
    for(bool ac:{true,false}){auto q=Basic(5);q.Axes={{-Pi/2,Pi/2,.2,1},{-2*Pi,2*Pi,.2,1}};
        for(std::size_t i=0;i<5;++i){const auto tilt=30*Pi/180,continuousC=(170+5*static_cast<double>(i))*Pi/180;const auto canonical=std::remainder(continuousC,2*Pi);
            auto& p=q.Waypoints[i];p.Original={Forward(ac,tilt,continuousC),{tilt,i==0||i==4?continuousC:canonical}};p.MaximumDeviationRadians=0;
            for(int winding=-1;winding<=1;++winding)p.Candidates.push_back({p.Original.Direction,{tilt,canonical+2*Pi*winding}});}
        q.Waypoints[3].Original.Axes[1]=185*Pi/180;q.Waypoints[3].Locked=true;
        q.CheckState=[&](auto,const auto& p){return Angle(p.Direction,Forward(ac,p.Axes[0],p.Axes[1]))<1e-10;};
        const auto r=service->Plan(q);ASSERT_EQ("Solved",r.Status)<<(ac?"AC":"BC");
        for(std::size_t i=0;i<5;++i){EXPECT_NEAR((170+5*static_cast<double>(i))*Pi/180,r.Orientations[i].Axes[1],1e-12);EXPECT_TRUE(q.CheckState(i,r.Orientations[i]));}
        EXPECT_EQ(q.Waypoints[3].Original.Axes,r.Orientations[3].Axes);
        q.Axes[1].Maximum=Pi;const auto bad=service->Plan(q);EXPECT_EQ("NoSolution",bad.Status);EXPECT_TRUE(bad.Orientations.empty());
    }
}
TEST(PathOrientation, SingularIJKDoesNotForceAnArbitraryCSpin)
{
    using namespace orientation_evaluation;Scene scene;auto q=Basic(4);q.Axes={{-Pi/2,Pi/2,.2,1},{-2*Pi,2*Pi,Pi,1}};
    for(auto& p:q.Waypoints){p.Original.Axes={0,0};p.MaximumDeviationRadians=0;p.Candidates={PathOrientation{{0,0,1},{0,0}}};}
    q.Waypoints[1].Original.Axes[1]=170*Pi/180;q.Waypoints[2].Original.Axes[1]=-170*Pi/180;
    const auto r=scene.Services().Resolve<IPathOrientationPlanningService>()->Plan(q);ASSERT_EQ("Solved",r.Status);for(const auto& p:r.Orientations)EXPECT_EQ(0,p.Axes[1]);
}
TEST(PathOrientation, InvalidInputsCancellationAndBudgetsNeverReturnAnOptimisticPath)
{
    using namespace orientation_evaluation;Scene scene;auto q=Basic(3);const auto service=scene.Services().Resolve<IPathOrientationPlanningService>();
    auto r=service->Plan(q,[]{return true;});EXPECT_EQ("Cancelled",r.Status);EXPECT_TRUE(r.Orientations.empty());
    q.MaximumSeconds=.001;q.CheckState=[](auto,const auto&){std::this_thread::sleep_for(std::chrono::milliseconds(3));return true;};r=service->Plan(q);EXPECT_EQ("Timeout",r.Status);EXPECT_TRUE(r.Orientations.empty());
    q=Basic(3);q.MaximumGraphEdges=1;r=service->Plan(q);EXPECT_EQ("GraphBudgetExceeded",r.Status);EXPECT_TRUE(r.Orientations.empty());
    q=Basic(3);q.Waypoints[1].Candidates={PathOrientation{Tilt(20),{}}};q.MaximumSearchIterations=1;q.CheckState=[](auto i,const auto& p){return i!=1||Angle(p.Direction,Tilt(20))<1e-9;};
    r=service->Plan(q);EXPECT_EQ("SearchBudgetExceeded",r.Status);EXPECT_TRUE(r.Orientations.empty());
    q=Basic(3);q.EditableRanges={{0,3}};EXPECT_THROW(service->Plan(q),std::invalid_argument);
    q=Basic(3);q.Waypoints[1].Original.Direction={0,0,0};EXPECT_THROW(service->Plan(q),std::invalid_argument);
}
