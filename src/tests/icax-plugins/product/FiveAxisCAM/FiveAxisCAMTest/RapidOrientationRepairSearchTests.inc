#include <FiveAxisCAM/FiveAxisRapidOrientationRepair.inc>
namespace rapid_orientation_search_tests {
    using Pose=iCAX::GeometryServices::TravelPose;
    using Field=RapidOrientationRepair::DirectionField;
    std::vector<Pose> Path(){std::vector<Pose> path;for(double x:{0.,1.,2.,4.,8.,10.,11.})path.push_back({{x,0,20},{0,0,1}});return path;}
    auto Transport(const Field& directions){
        namespace M=iCAX::GeometryServices::RigidMotion;M::Vector previous{0,0,1};M::Quaternion q{0,0,0,1};
        for(const auto& direction:directions){q=M::Multiply(M::Between(previous,direction),q);for(auto& x:q)x=std::round(x*1e14)/1e14;q=M::Unit(q);previous=direction;}return q;
    }
}
TEST(FiveAxisRapidOrientationSearch, SmoothDirectionCandidatesPreserveOriginalPointsEndpointsAndActualTransportClosure)
{
    using namespace rapid_orientation_search_tests;namespace M=iCAX::GeometryServices::RigidMotion;auto path=Path();const auto before=path;std::size_t accepted=0,progress=0;
    const auto result=RapidOrientationRepair::Search(path,3,[&](const Field& directions)->std::optional<double>{const auto q=Transport(directions);return 2*std::atan2(q[2],q[3]);},
        [&](const Field& directions){if(Transport(directions)!=M::Quaternion{0,0,0,1})return false;++accepted;EXPECT_EQ(path.front().Direction,directions.front());EXPECT_EQ(path.back().Direction,directions.back());return true;},[]{return false;},[&](auto count){progress=count;});
    ASSERT_TRUE(result.Directions);EXPECT_EQ(1u,accepted);EXPECT_EQ(result.Candidates,progress);EXPECT_FALSE(result.TimedOut);ASSERT_EQ(path.size(),result.Directions->size());
    for(std::size_t i=0;i<path.size();++i){EXPECT_EQ(before[i].Point,path[i].Point);EXPECT_EQ(before[i].Direction,path[i].Direction);EXPECT_NEAR(1.,M::Length(result.Directions->at(i)),1e-12);}
    const auto angle=[&](std::size_t i){return std::acos(std::clamp(M::Dot(path[i].Direction,result.Directions->at(i)),-1.,1.));};
    EXPECT_GT(angle(3),angle(2));EXPECT_GT(angle(2),angle(1));EXPECT_GT(angle(3),angle(4));EXPECT_GT(angle(4),angle(5));EXPECT_DOUBLE_EQ(0.,angle(0));EXPECT_DOUBLE_EQ(0.,angle(6));
}
TEST(FiveAxisRapidOrientationSearch, NonGridClosureRootIsFoundBeforeCollisionAcceptance)
{
    using namespace rapid_orientation_search_tests;const auto path=Path();std::size_t collisionCalls=0;const double slope=std::tan(.3);
    const auto residual=[&](const Field& directions){return directions[3][1]-slope*directions[3][0];};
    const auto result=RapidOrientationRepair::Search(path,3,[&](const Field& directions)->std::optional<double>{return residual(directions);},
        [&](const Field& directions){++collisionCalls;EXPECT_LE(std::abs(residual(directions)),1e-14);return true;},[]{return false;});
    ASSERT_TRUE(result.Directions);EXPECT_EQ(1u,collisionCalls);EXPECT_GT(result.Candidates,2u);EXPECT_NEAR(.3,std::atan2(result.Directions->at(3)[1],result.Directions->at(3)[0]),1e-10);
}
TEST(FiveAxisRapidOrientationSearch, UnclosedOrUnrepresentableDirectionsNeverReachCollisionAcceptance)
{
    using namespace rapid_orientation_search_tests;std::size_t accepted=0,closureCalls=0;
    const auto accept=[&](const Field&){++accepted;return true;};
    const auto unclosed=RapidOrientationRepair::Search(Path(),3,[&](const Field&)->std::optional<double>{++closureCalls;return .1;},accept,[]{return false;});
    EXPECT_FALSE(unclosed.Directions);EXPECT_EQ(closureCalls,unclosed.Candidates);EXPECT_GT(closureCalls,1u);EXPECT_EQ(0u,accepted);
    const auto unrepresentable=RapidOrientationRepair::Search(Path(),3,[](const Field&)->std::optional<double>{return {};},accept,[]{return false;});
    EXPECT_FALSE(unrepresentable.Directions);EXPECT_EQ(0u,accepted);
}
TEST(FiveAxisRapidOrientationSearch, CancellationStopsBeforeAnyFurtherValidationOrAcceptance)
{
    using namespace rapid_orientation_search_tests;bool stop=false;std::size_t closureCalls=0,collisionCalls=0;
    const auto result=RapidOrientationRepair::Search(Path(),3,[&](const Field&)->std::optional<double>{EXPECT_FALSE(stop);++closureCalls;return 0.;},
        [&](const Field&){EXPECT_FALSE(stop);++collisionCalls;return false;},[&]{return stop;},[&](auto count){if(count>=3)stop=true;});
    EXPECT_FALSE(result.Directions);EXPECT_TRUE(stop);EXPECT_GT(closureCalls,0u);EXPECT_GT(collisionCalls,0u);EXPECT_FALSE(result.TimedOut);
}
TEST(FiveAxisRapidOrientationSearch, ProtectedEndpointAndInvalidInputCannotBeAdjusted)
{
    using namespace rapid_orientation_search_tests;std::size_t callbacks=0;const auto closure=[&](const Field&)->std::optional<double>{++callbacks;return 0.;};const auto accept=[&](const Field&){++callbacks;return true;};
    const auto path=Path();EXPECT_FALSE(RapidOrientationRepair::Search(path,0,closure,accept,[]{return false;}).Directions);EXPECT_FALSE(RapidOrientationRepair::Search(path,path.size()-1,closure,accept,[]{return false;}).Directions);EXPECT_EQ(0u,callbacks);
    auto invalid=path;invalid[3].Direction={0,0,0};EXPECT_THROW(RapidOrientationRepair::Search(invalid,3,closure,accept,[]{return false;}),std::invalid_argument);EXPECT_EQ(0u,callbacks);
}
