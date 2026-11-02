namespace
{
    using OrderSolver=iCAX::FiveAxisCAM::Ordering::Solver;
    OrderSolver OrderingFixture(std::size_t count)
    {
        OrderSolver s;s.Nodes.push_back({});
        for(std::size_t i=0;i<count;++i){iCAX::FiveAxisCAM::Ordering::Cut c;c.ID=std::to_string(i);c.Number=i;c.Placement="part";c.Start=c.End={double(i)*10,0,0};s.Cuts.push_back(c);
            s.Nodes.push_back({{}, {i}});s.Nodes[0].Children.push_back(i+1);s.Nodes[0].Leaves.push_back(i);}
        return s;
    }
}
TEST(FiveAxisMachiningOrder, FreeStartDoesNotForceSmallestNumberAndImprovesWholeRoute)
{
    auto s=OrderingFixture(6);const std::array<double,6> x{30,0,10,20,40,50};for(std::size_t i=0;i<x.size();++i)s.Cuts[i].Start=s.Cuts[i].End={x[i],0,0};
    s.Prepare();const auto route=s.Solve(0,6,6);EXPECT_NE(0u,route.front());EXPECT_LT(s.Score(route,6,6),s.Score({0,1,2,3,4,5},6,6));
    EXPECT_TRUE(s.Valid(route));
}
TEST(FiveAxisMachiningOrder, ProcessDependenciesArePerPlacementRatherThanGlobalBatches)
{
    auto s=OrderingFixture(4);s.Cuts[0].Phase=0;s.Cuts[1].Phase=2;s.Cuts[2].Phase=0;s.Cuts[3].Phase=2;
    s.Cuts[2].Placement=s.Cuts[3].Placement="other";s.Cuts[2].Start=s.Cuts[2].End={1000,0,0};s.Cuts[3].Start=s.Cuts[3].End={1010,0,0};s.Prepare();
    const auto route=s.Solve(0,4,4);ASSERT_TRUE(s.Valid(route));
    EXPECT_EQ(s.Cuts[route[0]].Placement,s.Cuts[route[1]].Placement);EXPECT_EQ(0,s.Cuts[route[0]].Phase);EXPECT_EQ(2,s.Cuts[route[1]].Phase);
}
TEST(FiveAxisMachiningOrder, CombinedStrategyAllowsMarkAndHoleMixingButKeepsReleaseLast)
{
    auto s=OrderingFixture(4);s.Cuts[0].Phase=0;s.Cuts[1].Phase=0;s.Cuts[2].Phase=1;s.Cuts[3].Phase=2;
    const std::array<double,4> x{0,1000,10,1020};for(std::size_t i=0;i<x.size();++i)s.Cuts[i].Start=s.Cuts[i].End={x[i],0,0};
    s.Prepare();const auto separate=s.Solve(0,4,4);EXPECT_TRUE(s.Valid(separate));EXPECT_TRUE(s.Before(1,2));
    s.CombineMarkAndHole=true;s.Prepare();const auto combined=s.Solve(0,4,4);
    EXPECT_FALSE(s.Before(1,2));EXPECT_TRUE(s.Before(1,3));EXPECT_TRUE(s.Before(2,3));EXPECT_TRUE(s.Valid(combined));
    EXPECT_EQ(3u,combined.back());EXPECT_LT(std::find(combined.begin(),combined.end(),2u),std::find(combined.begin(),combined.end(),1u));
    EXPECT_LT(s.Score(combined,4,4),s.Score(separate,4,4));
    s.Nodes[1].Locked=s.Nodes[1].Anchored=true;s.Prepare();const auto locked=s.Solve(0,4,4);EXPECT_EQ(0u,locked.front());EXPECT_EQ(3u,locked.back());
}
TEST(FiveAxisMachiningOrder, NestedGroupsOptimizeInternallyAndLockedMemberAnchorsAllAncestors)
{
    auto s=OrderingFixture(7);s.Cuts[1].Start=s.Cuts[1].End={40,0,0};s.Cuts[2].Start=s.Cuts[2].End={20,0,0};s.Cuts[3].Start=s.Cuts[3].End={30,0,0};
    s.Nodes.push_back({{2,3,4},{1,2,3}});s.Nodes.push_back({{8,5},{1,2,3,4}});s.Nodes[0].Children={1,9,6,7};
    s.Nodes[1].Locked=s.Nodes[1].Anchored=true;s.Nodes[6].Locked=s.Nodes[6].Anchored=true;s.Prepare();
    const auto route=s.Solve(0,7,7);ASSERT_EQ(7u,route.size());EXPECT_EQ(0u,route[0]);EXPECT_EQ(5u,route[5]);
    const auto at=[&](std::size_t id){return std::find(route.begin(),route.end(),id)-route.begin();};
    EXPECT_EQ(2, std::max({at(1),at(2),at(3)})-std::min({at(1),at(2),at(3)}));
    EXPECT_LT(s.Score(route,7,7),s.Score({0,1,2,3,4,5,6},7,7));
    s.Nodes[3].Locked=s.Nodes[3].Anchored=s.Nodes[8].Anchored=s.Nodes[9].Anchored=true;
    const auto anchored=s.Solve(0,7,7);EXPECT_EQ(2u,anchored[2]);EXPECT_EQ(5u,anchored[5]);
}
TEST(FiveAxisMachiningOrder, GroupsMoveAsWholeUnitsAndTheirMembersAlsoReorder)
{
    auto s=OrderingFixture(6);const std::array<double,6> x{0,90,80,20,10,100};
    for(std::size_t i=0;i<x.size();++i)s.Cuts[i].Start=s.Cuts[i].End={x[i],0,0};
    s.Nodes.push_back({{2,3},{1,2}});s.Nodes.push_back({{4,5},{3,4}});s.Nodes[0].Children={1,7,8,6};
    s.Nodes[1].Locked=s.Nodes[1].Anchored=s.Nodes[6].Locked=s.Nodes[6].Anchored=true;s.Prepare();
    const auto route=s.Solve(0,6,6);
    EXPECT_EQ((std::vector<std::size_t>{0,4,3,2,1,5}),route);
    EXPECT_LT(s.Score(route,6,6),s.Score({0,1,2,3,4,5},6,6));
}
TEST(FiveAxisMachiningOrder, GroupCyclesAndLockDependencyConflictsRejectWithoutReordering)
{
    auto s=OrderingFixture(3);s.Cuts[0].Phase=0;s.Cuts[1].Phase=1;s.Cuts[2].Phase=2;
    s.Nodes.push_back({{1,3},{0,2}});s.Nodes[0].Children={4,2};s.Prepare();EXPECT_THROW(s.Solve(0,3,3),std::invalid_argument);
    s=OrderingFixture(3);s.Cuts[0].Phase=2;s.Cuts[1].Phase=1;s.Nodes[1].Locked=s.Nodes[1].Anchored=true;s.Prepare();EXPECT_THROW(s.Solve(0,3,3),std::invalid_argument);
}
TEST(FiveAxisMachiningOrder, ToolDirectionAndConfiguredAngularDynamicsAffectTravelCost)
{
    auto s=OrderingFixture(3);s.Cuts[1].Start=s.Cuts[1].End={0,0,0};s.Cuts[1].StartDirection=s.Cuts[1].EndDirection={0,0,-1};
    s.Settings.AngularSpeed=.1;s.Prepare();EXPECT_GT(s.Costs[0][1],s.Costs[0][2]);
    s.Settings.AngularSpeed=10;s.Settings.AngularAcceleration=100;s.Prepare();EXPECT_LT(s.Costs[0][1],10.);
}
TEST(FiveAxisMachiningOrder, DisabledCutsStayInPermutationAndDoNotCreateMachiningDependencies)
{
    auto s=OrderingFixture(4);s.Cuts[0].Enabled=false;s.Cuts[0].Phase=2;s.Prepare();const auto route=s.Solve(0,4,4);
    EXPECT_EQ(4u,route.size());EXPECT_TRUE(s.Valid(route));EXPECT_EQ(1,std::count(route.begin(),route.end(),0u));
}
TEST(FiveAxisMachiningOrder, FullSupportedScopeRemainsBoundedAndNeverWorsensFeasibleExistingRoute)
{
    auto s=OrderingFixture(256);for(std::size_t i=0;i<256;++i)s.Cuts[i].Start=s.Cuts[i].End={double((i*73)%256),double((i*37)%89),double(i%11)};
    s.Prepare();const auto route=s.Solve(0,256,256);EXPECT_EQ(256u,route.size());EXPECT_TRUE(s.Valid(route));EXPECT_LE(s.Score(route,256,256),s.Score(s.Nodes[0].Leaves,256,256)+1e-8);
}
TEST(FiveAxisMachiningOrder, DeepNestedGroupsHaveABoundedContextRefinementBudget)
{
    auto s=OrderingFixture(34);std::size_t child=1;
    for(std::size_t i=1;i<34;++i){const auto node=s.Nodes.size();std::vector<std::size_t> leaves;for(std::size_t j=0;j<=i;++j)leaves.push_back(j);
        s.Nodes.push_back({{child,i+1},leaves});child=node;}
    s.Nodes[0].Children={child};s.Prepare();const auto route=s.Solve(0,34,34);
    EXPECT_EQ(34u,route.size());EXPECT_TRUE(s.Valid(route));EXPECT_LE(s.Refinements,32u);
}
TEST(FiveAxisMachiningOrder, CandidateTiesUseCutIdentityRatherThanThePreviousManualSequence)
{
    auto s=OrderingFixture(9);for(std::size_t i=0;i<9;++i)s.Cuts[i].Phase=int(i%3);s.Prepare();const auto first=s.Solve(0,9,9);
    std::reverse(s.Nodes[0].Children.begin(),s.Nodes[0].Children.end());std::reverse(s.Nodes[0].Leaves.begin(),s.Nodes[0].Leaves.end());
    EXPECT_EQ(first,s.Solve(0,9,9));
}
TEST(FiveAxisMachiningOrder, SpatialRowsDirectionAndSkippingRemainHardConstraints)
{
    auto s=OrderingFixture(12);for(std::size_t i=0;i<12;++i)s.Cuts[i].Center=s.Cuts[i].Start=s.Cuts[i].End={double(i%4)*10,double(i/4)*10,0};
    s.Pattern="Z";s.Prepare();EXPECT_EQ((std::vector<std::size_t>{0,1,2,3,4,5,6,7,8,9,10,11}),s.Solve(0,12,12));
    s.Pattern="Snake";s.Prepare();EXPECT_EQ((std::vector<std::size_t>{0,1,2,3,7,6,5,4,8,9,10,11}),s.Solve(0,12,12));
    s.Pattern="Z";s.Skip=1;s.Prepare();EXPECT_EQ((std::vector<std::size_t>{0,2,1,3,4,6,5,7,8,10,9,11}),s.Solve(0,12,12));
    s.Nodes[3].Locked=s.Nodes[3].Anchored=true;EXPECT_THROW(s.Solve(0,12,12),std::invalid_argument);
}
TEST(FiveAxisMachiningOrder, RingsGlobalProcessAndDistanceObjectiveHaveSeparateSemantics)
{
    auto s=OrderingFixture(9);for(std::size_t i=0;i<9;++i)s.Cuts[i].Center=s.Cuts[i].Start=s.Cuts[i].End={double(i%3)*10,double(i/3)*10,0};
    s.Pattern="Ring";s.Prepare();EXPECT_EQ(4u,s.Solve(0,9,9).back());s.Inward=false;s.Prepare();EXPECT_EQ(4u,s.Solve(0,9,9).front());
    s=OrderingFixture(2);s.Cuts[0].Placement="A";s.Cuts[1].Placement="B";s.Cuts[0].Phase=0;s.Cuts[1].Phase=1;s.Prepare();EXPECT_FALSE(s.Before(0,1));s.GlobalProcess=true;s.Prepare();EXPECT_TRUE(s.Before(0,1));
    s.Settings.Retract=false;s.Settings.Lift=0;s.Cuts[1].Start=s.Cuts[0].End;s.Cuts[1].StartDirection={0,0,-1};s.Prepare();EXPECT_GT(s.Costs[0][1],0);s.DistanceObjective=true;s.Prepare();EXPECT_DOUBLE_EQ(0,s.Costs[0][1]);
}
TEST(FiveAxisMachiningOrder, ExplicitOriginIncludesItsAxisAndFreeStartRemainsUnbiased)
{
    auto s=OrderingFixture(2);s.Settings.Retract=false;s.Settings.Lift=0;s.UseOrigin=true;s.Cuts[0].StartDirection={0,0,-1};s.Prepare();
    EXPECT_GT(s.Connection(s.Cuts.size()+1,0),0);s.OriginDirection={0,0,-1};EXPECT_DOUBLE_EQ(0,s.Connection(s.Cuts.size()+1,0));
    s.OriginDirection={0,0,1};s.UseOrigin=false;EXPECT_DOUBLE_EQ(0,s.Connection(s.Cuts.size()+1,0));
}
TEST(FiveAxisMachiningOrder, HeightPenaltyReducesLayerSwitchingForDistanceAndTime)
{
    for(const bool distance:{true,false}){
        auto s=OrderingFixture(4);s.DistanceObjective=distance;s.Settings.Lift=0;s.Settings.Retract=false;
        for(std::size_t i=0;i<4;++i)s.Cuts[i].Start=s.Cuts[i].End={double(i%2)*6,0,double(i/2)*3};
        const auto height=[&](const auto& route){double sum=0;for(std::size_t i=1;i<route.size();++i)sum+=std::abs(s.Cuts[route[i-1]].End[2]-s.Cuts[route[i]].Start[2]);return sum;};
        s.Prepare();const auto flat=s.Solve(0,4,4);EXPECT_DOUBLE_EQ(6,height(flat));
        s.HeightPenaltyWeight=3;s.Prepare();const auto preferred=s.Solve(0,4,4);
        EXPECT_DOUBLE_EQ(3,height(preferred));EXPECT_LT(s.Score(preferred,4,4),s.Score(flat,4,4));EXPECT_TRUE(s.Valid(preferred));
    }
}
TEST(FiveAxisMachiningOrder, HeightPenaltyIsSoftAndUsesSetupZRatherThanSortingPlane)
{
    auto s=OrderingFixture(3);s.DistanceObjective=true;s.Settings.Lift=0;s.Settings.Retract=false;s.HeightPenaltyWeight=3;
    s.Cuts[1].Start=s.Cuts[1].End={1000,0,0};s.Cuts[2].Start=s.Cuts[2].End={1,0,3};s.Nodes[1].Locked=s.Nodes[1].Anchored=true;
    s.PlaneX={0,1,0};s.PlaneY={0,0,1};s.Prepare();EXPECT_EQ((std::vector<std::size_t>{0,2,1}),s.Solve(0,3,3));
    EXPECT_NEAR(std::sqrt(10.)+9.,s.Costs[0][2],1e-10);
    s.UseOrigin=true;s.Origin={1,0,-1};EXPECT_NEAR(16.,s.Connection(4,2),1e-10);
    s.HeightPenaltyWeight=0;s.Prepare();EXPECT_NEAR(std::sqrt(10.),s.Costs[0][2],1e-10);
}
TEST(FiveAxisMachiningOrder, HeightPenaltyDoesNotOverrideGroupLocksOrMachiningDependencies)
{
    auto s=OrderingFixture(5);s.DistanceObjective=true;s.Settings.Lift=0;s.Settings.Retract=false;s.HeightPenaltyWeight=3;
    s.Cuts[0].Phase=0;s.Cuts[0].Start=s.Cuts[0].End={0,0,10};s.Cuts[4].Phase=2;
    s.Cuts[1].Start=s.Cuts[1].End={10,0,8};s.Cuts[2].Start=s.Cuts[2].End={20,0,0};s.Cuts[3].Start=s.Cuts[3].End={30,0,8};
    s.Nodes.push_back({{2,3},{1,2},true,true});s.Nodes[0].Children={1,6,4,5};s.Prepare();const auto order=s.Solve(0,5,5);
    EXPECT_EQ((std::vector<std::size_t>{0,1,2,3,4}),order);EXPECT_TRUE(s.Valid(order));
}
